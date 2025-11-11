#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Configuração da Arquitetura
#define MAX_INSTR_MEM 16
#define QTD_ESTACOES 10
#define TAM_FILA_ROB 10
#define QTD_REGISTRADORES 8
#define N_ISSUE_POR_CICLO 8
#define N_COMMIT_POR_CICLO 8

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
    int tag_j, tag_k;
    int val_j, val_k;
    int rob_destino; 
    int cycles_left;
    bool ocupado;
} SlotReserva;

SlotReserva estacoes_reserva[QTD_ESTACOES];

// Item do Buffer de Reordenação (ROB)
typedef struct {
    OpType op;
    int reg_arq_dest;
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

// Unidade de Controle
typedef struct {
    int pc;
    int rob_head;
    int rob_tail;
    int rob_contagem;
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
            er->ocupado ? nome_operacao(er->op) : "",
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
    int emitidas = 0;

    while (emitidas < N_ISSUE_POR_CICLO && cpu_core.pc < instr_count) {
        Operacao instr_atual = memoria_instrucoes[cpu_core.pc];
        if (instr_atual.op == HALT) {
            return;
        }

        if (rob_cheio()) {
            printf("Stall: ROB cheio.\n");
            return;
        }

        int er_idx = encontrar_er_livre();
        if (er_idx == -1) {
            printf("Stall: Estacoes de reserva cheias.\n");
            return;
        }

        // Aloca entrada no ROB
        int rob_idx = cpu_core.rob_tail;
        fila_reordenacao[rob_idx].op = instr_atual.op;
        fila_reordenacao[rob_idx].reg_arq_dest = instr_atual.rd;
        fila_reordenacao[rob_idx].pronto = false;
        fila_reordenacao[rob_idx].em_uso = true;

        cpu_core.rob_tail = (cpu_core.rob_tail + 1) % TAM_FILA_ROB;
        cpu_core.rob_contagem++;

        // Preenche Estação de Reserva
        SlotReserva *er = &estacoes_reserva[er_idx];
        er->ocupado = true;
        er->op = instr_atual.op;
        er->rob_destino = rob_idx;
        er->cycles_left = 0;
        er->tag_j = er->tag_k = -1;
        er->val_j = er->val_k = 0;

        // Dependências
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && !fila_reordenacao[i].pronto) {
                if (fila_reordenacao[i].reg_arq_dest == instr_atual.rs1)
                    er->tag_j = i;
                if (fila_reordenacao[i].reg_arq_dest == instr_atual.rs2)
                    er->tag_k = i;
            }
        }

        if (er->tag_j == -1) er->val_j = registradores_arq.regs[instr_atual.rs1];
        if (instr_atual.op == LI) {
            er->val_k = instr_atual.rs2;
            er->tag_k = -1;
        } else if (er->tag_k == -1)
            er->val_k = registradores_arq.regs[instr_atual.rs2];

        printf("Issue: PC=%d -> ER[%d], ROB[%d], R%d = R%d %s R%d\n",
            cpu_core.pc, er_idx, rob_idx, instr_atual.rd,
            instr_atual.rs1, nome_operacao(instr_atual.op), instr_atual.rs2);

        cpu_core.pc++;
        emitidas++;
    }
}

// Estágio 2: Execução
void etapa_execucao() {
    for (int i = 0; i < QTD_ESTACOES; i++) {
        SlotReserva *unidade = &estacoes_reserva[i];

        if (!unidade->ocupado) continue;
        
        if (unidade->tag_j != -1 || unidade->tag_k != -1)
            continue;

        if (unidade->cycles_left == 0)
            unidade->cycles_left = latency_for_op(unidade->op);

        unidade->cycles_left--;

        if (unidade->cycles_left == 0) {
            int resultado = 0;
            switch (unidade->op) {
                case ADD: resultado = unidade->val_j + unidade->val_k; break;
                case SUB: resultado = unidade->val_j - unidade->val_k; break;
                case MUL: resultado = unidade->val_j * unidade->val_k; break;
                case DIV: resultado = (unidade->val_k ? unidade->val_j / unidade->val_k : 0); break;
                case LI:  resultado = unidade->val_j + unidade->val_k; break;
                default: break;
            }

            fila_reordenacao[unidade->rob_destino].valor = resultado;
            fila_reordenacao[unidade->rob_destino].pronto = true;

            printf("Execute: ER[%d] (%s) -> ROB[%d] (Resultado: %d)\n",
                   i, nome_operacao(unidade->op), unidade->rob_destino, resultado);

            unidade->ocupado = false;
            unidade->tag_j = unidade->tag_k = -1;
            unidade->val_j = unidade->val_k = 0;
            unidade->cycles_left = 0;
        } else {
            printf("Executing: ER[%d] (%s) cycles_left=%d\n",
                   i, nome_operacao(unidade->op), unidade->cycles_left);
        }
    }
}

// Estágio 3/4: Escrita e Commit
void etapa_finalizacao() {
    // Broadcast simultâneo
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

    // Commit de até N instruções
    int commits = 0;
    while (commits < N_COMMIT_POR_CICLO) {
        int head_idx = cpu_core.rob_head;
        if (!(fila_reordenacao[head_idx].em_uso && fila_reordenacao[head_idx].pronto))
            break;

        int dest_reg = fila_reordenacao[head_idx].reg_arq_dest;
        int val_final = fila_reordenacao[head_idx].valor;
        registradores_arq.regs[dest_reg] = val_final;

        printf("Commit: R%d <- %d (ROB[%d])\n", dest_reg, val_final, head_idx);

        fila_reordenacao[head_idx].em_uso = false;
        fila_reordenacao[head_idx].pronto = false;
        cpu_core.rob_head = (cpu_core.rob_head + 1) % TAM_FILA_ROB;
        cpu_core.rob_contagem--;
        commits++;
    }
}

// Main

int main() {
    FILE *fp = fopen("simulacao.txt", "r");
    if (fp == NULL) {
        perror("Erro ao abrir 'simulacao.txt'");
        return 1;
    }

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

        if (instr.op == HALT) {
            instr.rd = instr.rs1 = instr.rs2 = 0;
            memoria_instrucoes[instr_count++] = instr;
            break;
        } else if (instr.op == LI) {
            sscanf(line, "%*s R%d , R%d ( %d )", &rd, &rs, &imm);
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = imm;
        } else {
            sscanf(line, "%*s R%d , R%d , R%d", &rd, &rs, &rt);
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = rt;
        }

        memoria_instrucoes[instr_count++] = instr;
    }
    fclose(fp);

    bool halt_detectado = false;

    while (true) {
        if (cpu_core.pc >= instr_count) break;

        printf("Ciclo %d\n", cpu_core.ciclo);
        mostrar_banco_regs();
        mostrar_estacoes_reserva();

        etapa_despacho(instr_count);
        etapa_execucao();
        etapa_finalizacao();

        cpu_core.ciclo++;
        printf("\n");

        if (cpu_core.ciclo > 100) {
            printf("Simulacao excedeu 100 ciclos. Abortando.\n");
            break;
        }

        if (memoria_instrucoes[cpu_core.pc].op == HALT && cpu_core.rob_contagem == 0)
            break;
    }

    printf("ESTADO FINAL\n");
    mostrar_regs_final();
    return 0;
}
