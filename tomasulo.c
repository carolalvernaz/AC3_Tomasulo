#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Configuração da Arquitetura
#define MAX_INSTR_MEM 16
#define QTD_ESTACOES 4
#define TAM_FILA_ROB 4
#define QTD_REGISTRADORES 8

// Estruturas de Dados
// Tipos de operação
typedef enum { ADD, SUB, MUL, DIV, LI, HALT } OpType;

// Instrução em "memória"
typedef struct {
    int op;
    int rs1;
    int rs2; 
    int rd;
} Operacao;

Operacao memoria_instrucoes[MAX_INSTR_MEM];

// Estação de Reserva (ER)
typedef struct {
    OpType op;
    int tag_j, tag_k; // Tags do ROB (qj, qk)
    int val_j, val_k; // Valores dos operandos
    int rob_destino; 
    int cycles_left;
    bool ocupado;
} SlotReserva;

SlotReserva estacoes_reserva[QTD_ESTACOES];

// Item do Buffer de Reordenação (ROB)
typedef struct {
    OpType op;
    int reg_arq_dest; // Registrador de destino
    int valor;
    bool pronto; 
    bool em_uso; 
} ItemROB;

ItemROB fila_reordenacao[TAM_FILA_ROB];

// Arquivo de Registradores
typedef struct {
    int regs[QTD_REGISTRADORES];
} ArquivoRegs;

ArquivoRegs registradores_arq = {{0}};

// Unidade de Controle (Estado da CPU)
typedef struct {
    int pc;
    int rob_head; // Ponteiro de commit
    int rob_tail; // Ponteiro de issue
    int rob_contagem; // Ocupação
    int ciclo;
} UnidadeControle;

UnidadeControle cpu_core = {0, 0, 0, 0, 1};

// Funções Auxiliares

bool rob_cheio() {
    return cpu_core.rob_contagem >= TAM_FILA_ROB;
}

int encontrar_er_livre() {
    for (int i = 0; i < QTD_ESTACOES; i++) {
        if (!estacoes_reserva[i].ocupado)
            return i;
    }
    return -1;
}

OpType decodificar_mnemonico(const char *mnemonic) {
    if (strcmp(mnemonic, "ADD") == 0) return ADD;
    if (strcmp(mnemonic, "MUL") == 0) return MUL;
    if (strcmp(mnemonic, "SUB") == 0) return SUB;
    if (strcmp(mnemonic, "DIV") == 0) return DIV;
    if (strcmp(mnemonic, "LW") == 0) return LI; 
    if (strcmp(mnemonic, "HALT") == 0) return HALT;
    fprintf(stderr, "Instrucao desconhecida: %s\n", mnemonic);
    return HALT;
}

const char* nome_operacao(OpType op) {
    switch (op) {
        case ADD: return "ADD";
        case SUB: return "SUB";
        case MUL: return "MUL";
        case DIV: return "DIV";
        case LI:  return "LW";
        case HALT: return "HALT";
        default: return "???";
    }
}

int latency_for_op(OpType op) {
    switch (op) {
        case LI:  return 1;
        case ADD: return 1;
        case SUB: return 1;
        case MUL: return 2;
        case DIV: return 2;
        default:  return 1;
    }
}

// Funções de Impressão

void mostrar_banco_regs() {
    printf("Registradores: ");
    for (int i = 0; i < QTD_REGISTRADORES; i++) {
        printf("R%d = %d", i, registradores_arq.regs[i]);
        if (i < QTD_REGISTRADORES - 1) {
            printf(", ");
        }
    }
    printf("\n");
}

void mostrar_estacoes_reserva() {
    printf("------ Estado das Estacoes de Reserva ------\n");
    printf("ID | Op  | Busy | ROB | Vj | Vk | Qj | Qk\n");
    printf("--------------------------------------------\n");
    for (int i = 0; i < QTD_ESTACOES; i++) {
        SlotReserva *er = &estacoes_reserva[i];
        printf("%2d | %-3s |  %3s | %3d | %2d | %2d | %2d | %2d\n",
            i,
            nome_operacao(er->op),
            er->ocupado ? "Sim" : "Nao",
            er->rob_destino,
            er->val_j,
            er->val_k,
            er->tag_j,
            er->tag_k
        );
    }
    printf("--------------------------------------------\n");
}

void mostrar_regs_final() {
    printf("Registradores: ");
     for (int i = 0; i < QTD_REGISTRADORES; i++) {
        printf("R%d = %d ", i, registradores_arq.regs[i]);
    }
    printf("\n");
}

// Estágios do Pipeline

// Estágio 1: Despacho (Issue)
void etapa_despacho(int instr_count) {
    if (cpu_core.pc >= instr_count) return;
    Operacao instr_atual = memoria_instrucoes[cpu_core.pc];
    if (instr_atual.op == HALT) {
        return;
    }
    
    int er_idx = encontrar_er_livre();

    if (rob_cheio()) {
        printf("Stall: ROB cheio.\n");
        return; 
    }
    if (er_idx == -1) {
        printf("Stall: Estacoes de reserva cheias.\n");
        return;
    }

    // Aloca entrada no ROB e na ER
    int rob_idx = cpu_core.rob_tail;
    fila_reordenacao[rob_idx].op = instr_atual.op;
    fila_reordenacao[rob_idx].reg_arq_dest = instr_atual.rd;
    fila_reordenacao[rob_idx].pronto = false;
    fila_reordenacao[rob_idx].em_uso = true;

    cpu_core.rob_tail = (cpu_core.rob_tail + 1) % TAM_FILA_ROB;
    cpu_core.rob_contagem++;
    
    estacoes_reserva[er_idx].ocupado = true;
    estacoes_reserva[er_idx].op = instr_atual.op;
    estacoes_reserva[er_idx].rob_destino = rob_idx;
    estacoes_reserva[er_idx].cycles_left = 0;
    // Busca operandos
    if (instr_atual.op == LI) {
        // LW: verifica dependência do registrador base (rs1)
        estacoes_reserva[er_idx].tag_j = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs1 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_j = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_j == -1)
            estacoes_reserva[er_idx].val_j = registradores_arq.regs[instr_atual.rs1];
        // Offset (rs2) é imediato, sem dependência
        estacoes_reserva[er_idx].tag_k = -1;
        estacoes_reserva[er_idx].val_k = instr_atual.rs2;
    } else {
        // Operando J (rs1)
        estacoes_reserva[er_idx].tag_j = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs1 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_j = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_j == -1)
            estacoes_reserva[er_idx].val_j = registradores_arq.regs[instr_atual.rs1];
        
        // Operando K (rs2)
        estacoes_reserva[er_idx].tag_k = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs2 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_k = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_k == -1)
            estacoes_reserva[er_idx].val_k = registradores_arq.regs[instr_atual.rs2];
    }
    
    const char *op_str = (instr_atual.op == ADD) ? "op" : (instr_atual.op == SUB) ? "op" : (instr_atual.op == MUL) ? "op" : (instr_atual.op == DIV) ? "op" : "<-";
     if (instr_atual.op == LI) {
         printf("Issue: PC=%d -> ER[%d], ROB[%d], R%d = R%d (%d)\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.rd, instr_atual.rs1, instr_atual.rs2);
    } else {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], R%d = R%d %s R%d\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.rd, instr_atual.rs1, op_str, instr_atual.rs2);
    }    
    cpu_core.pc++;
}

// Estágio 2: Execução
void etapa_execucao() {
    for (int i = 0; i < QTD_ESTACOES; i++) {
        SlotReserva *unidade = &estacoes_reserva[i];

         if (!unidade->ocupado) continue;
        
          // Se ainda esperando operandos, não começa a contagem
        if (unidade->tag_j != -1 || unidade->tag_k != -1) {
            continue;
        }

        // Se ainda não começou a executar (cycles_left == 0), inicia o contador
        if (unidade->cycles_left == 0) {
            unidade->cycles_left = latency_for_op(unidade->op);
        }

        // Decrementa um ciclo de execução
        unidade->cycles_left--;

        // Se acabou de terminar (cycles_left == 0), gera resultado
        if (unidade->cycles_left == 0) {
            int resultado = 0;
            switch (unidade->op) {
                case ADD: resultado = unidade->val_j + unidade->val_k; break;
                case SUB: resultado = unidade->val_j - unidade->val_k; break;
                case MUL: resultado = unidade->val_j * unidade->val_k; break;
                case DIV: resultado = (unidade->val_k ? unidade->val_j / unidade->val_k : 0); break;
                case LI:  resultado = unidade->val_j + unidade->val_k; break; // LW: base + offset
                default: break;
            }
            
            fila_reordenacao[unidade->rob_destino].valor = resultado;
            fila_reordenacao[unidade->rob_destino].pronto = true;
            
printf("Execute: ER[%d] (%s) -> ROB[%d] (Resultado: %d)\n",
       i, nome_operacao(unidade->op), unidade->rob_destino, resultado);
            unidade->ocupado = false; // Libera ER
             unidade->tag_j = unidade->tag_k = -1;
            unidade->val_j = unidade->val_k = 0;
            unidade->cycles_left = 0;
        } else {
            // ainda em execução
            printf("Executing: ER[%d] (%s) cycles_left=%d\n",
                   i, nome_operacao(unidade->op), unidade->cycles_left);
        }
    }
}

// Estágio 3/4: Escrita (CDB) e Finalização (Commit)
void etapa_finalizacao() {
    
    // Parte 1: Escrita no CDB (Broadcast)
    for (int i = 0; i < TAM_FILA_ROB; i++) {
        if (fila_reordenacao[i].em_uso && fila_reordenacao[i].pronto) {
            for (int j = 0; j < QTD_ESTACOES; j++) {
                if (estacoes_reserva[j].ocupado) {
                    if (estacoes_reserva[j].tag_j == i) {
                        estacoes_reserva[j].val_j = fila_reordenacao[i].valor;
                        estacoes_reserva[j].tag_j = -1;
                    }
                    if (estacoes_reserva[j].tag_k == i) {
                        estacoes_reserva[j].val_k = fila_reordenacao[i].valor;
                        estacoes_reserva[j].tag_k = -1;
                    }
                }
            }
        }
    }

    // Parte 2: Commit (em ordem)
    int head_idx = cpu_core.rob_head;
    if (fila_reordenacao[head_idx].em_uso && fila_reordenacao[head_idx].pronto) {
        
        int dest_reg = fila_reordenacao[head_idx].reg_arq_dest;
        int val_final = fila_reordenacao[head_idx].valor;

        // Atualiza Arquivo de Registradores
        registradores_arq.regs[dest_reg] = val_final;

        printf("Commit: R%d <- %d (ROB[%d])\n", dest_reg, val_final, head_idx);

        // Libera entrada do ROB e avança o ponteiro
        fila_reordenacao[head_idx].em_uso = false;
        fila_reordenacao[head_idx].pronto = false; 

        cpu_core.rob_head = (cpu_core.rob_head + 1) % TAM_FILA_ROB;
        cpu_core.rob_contagem--;
    }
}

// Main

int main() {
    FILE *fp = fopen("simulacao.txt", "r");
    if (fp == NULL) {
        perror("Erro ao abrir 'simulacao.txt'");
        return 1;
    }

    // Carrega instruções
    char line[100];
    int instr_count = 0;
    while (fgets(line, sizeof(line), fp) && instr_count < MAX_INSTR_MEM) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';

        char mnemonic[10];
        int rd = -1, rs = -1, rt = -1, imm = 0;

       if (sscanf(line, " %15s", mnemonic) != 1) continue;

        Operacao instr;
        instr.op = decodificar_mnemonico(mnemonic);

        // HALT
        if (instr.op == HALT) {
            instr.rd = instr.rs1 = instr.rs2 = 0;
            memoria_instrucoes[instr_count++] = instr;
            break;
        }
        // LW
        else if (instr.op == LI) {
            if (sscanf(line, "%*s R%d , R%d ( %d )", &rd, &rs, &imm) != 3 &&
                sscanf(line, "%*s R%d, R%d (%d)", &rd, &rs, &imm) != 3 &&
                sscanf(line, "%*s R%d , R%d (%d)", &rd, &rs, &imm) != 3 &&
                sscanf(line, "%*s R%d, R%d ( %d )", &rd, &rs, &imm) != 3) {
                fprintf(stderr, "Erro ao ler LW (linha %d): %s\n", instr_count+1, line);
                continue;
            }
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = imm;
        }
        // Operacoes R-R-R
        else {
            if (sscanf(line, "%*s R%d , R%d , R%d", &rd, &rs, &rt) != 3 &&
                sscanf(line, "%*s R%d, R%d, R%d", &rd, &rs, &rt) != 3) {
                fprintf(stderr, "Erro ao ler instrucao (linha %d): %s\n", instr_count+1, line);
                continue;
            }
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = rt;
        }

        if (instr.rd < 0 || instr.rs1 < 0 ||
            instr.rd >= QTD_REGISTRADORES || instr.rs1 >= QTD_REGISTRADORES) {
            fprintf(stderr, "Erro: registrador fora do intervalo (linha %d): %s\n", instr_count+1, line);
            fclose(fp);
            return 1;
        }
   
        if (instr.op != LI) {
            if (instr.rs2 < 0 || instr.rs2 >= QTD_REGISTRADORES) {
                fprintf(stderr, "Erro: registrador fora do intervalo (linha %d): %s\n", instr_count+1, line);
                fclose(fp);
                return 1;
            }
        }
        printf("Instrucao lida [%d]: %s -> rd=R%d rs1=R%d rs2=%d\n", instr_count, mnemonic, instr.rd, instr.rs1, instr.rs2);

        memoria_instrucoes[instr_count++] = instr;
    }
    fclose(fp);

    // Loop principal da simulação
    bool halt_detectado = false;
    
    while (true) {      
        if (cpu_core.pc >= instr_count) {
            printf("Fim da memoria de instrucoes alcancado.\n");
            break;
        }

        if (memoria_instrucoes[cpu_core.pc].op == HALT) {
            halt_detectado = true;
        }

        // Condição de parada
        if (halt_detectado && cpu_core.rob_contagem == 0) {
            break; 
        }

        printf("Ciclo %d\n", cpu_core.ciclo);
        mostrar_banco_regs();
        mostrar_estacoes_reserva();

        if (!halt_detectado) {
            etapa_despacho(instr_count); // Envia instrução para ROB e Estação de Reserva
        }
        etapa_execucao();
        etapa_finalizacao();

        cpu_core.ciclo++;
        printf("\n"); 

        if (cpu_core.ciclo > 100) {
            printf("Simulacao excedeu 100 ciclos. Abortando.\n");
            break;
        }
    }

    printf("ESTADO FINAL\n");
    mostrar_regs_final();
    return 0;
}
