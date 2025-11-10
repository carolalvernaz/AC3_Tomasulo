#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Configuração da Arquitetura
#define MAX_INSTR_MEM 16
#define QTD_ESTACOES 4
#define TAM_FILA_ROB 4
#define QTD_REGISTRADORES 8
#define MAX_LABELS 32
#define MAX_LABEL_NAME 32
#define TAM_MEMORIA_DADOS 256

// Estruturas de Dados

// Tipos de operação
typedef enum { ADD, SUB, MUL, DIV, LI, SW, J, JAL, BEQ, BNE, BLT, BGT, HALT } OpType;

// Tabela de labels
typedef struct {
    char nome[MAX_LABEL_NAME];
    int endereco;
} LabelEntry;

LabelEntry tabela_labels[MAX_LABELS];
int num_labels = 0;

// Instrução em "memória"
typedef struct {
    int op;
    int rs1;
    int rs2; 
    int rd;
    int immediate; // Para desvios: endereço de destino ou offset
    char label_name[MAX_LABEL_NAME]; // Nome do label (se houver)
} Operacao;

Operacao memoria_instrucoes[MAX_INSTR_MEM];

// Estação de Reserva (ER)
typedef struct {
    OpType op;
    int tag_j, tag_k; // Tags do ROB (qj, qk)
    int val_j, val_k; // Valores dos operandos
    int rob_destino; 
    int cycles_left;
    int pc_origem; // PC da instrução original (para desvios)
    int target_pc; // Endereço de destino do desvio (calculado na execução)
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
    int pc_origem; // PC da instrução original (para desvios)
    int target_pc; // Endereço de destino do desvio
    bool branch_taken; // Se branch foi tomado
} ItemROB;

ItemROB fila_reordenacao[TAM_FILA_ROB];

// Arquivo de Registradores
typedef struct {
    int regs[QTD_REGISTRADORES];
} ArquivoRegs;

ArquivoRegs registradores_arq = {{0}};

// Memória de Dados
int memoria_dados[TAM_MEMORIA_DADOS];

// Unidade de Controle (Estado da CPU)
typedef struct {
    int pc;
    int rob_head; // Ponteiro de commit
    int rob_tail; // Ponteiro de issue
    int rob_contagem; // Ocupação
    int ciclo;
} UnidadeControle;

UnidadeControle cpu_core = {0, 0, 0, 0, 1};

// Variável global para número de instruções carregadas
int total_instrucoes = 0;

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
    if (strcmp(mnemonic, "LD") == 0 || strcmp(mnemonic, "LW") == 0) return LI;
    if (strcmp(mnemonic, "SW") == 0) return SW;
    if (strcmp(mnemonic, "J") == 0) return J;
    if (strcmp(mnemonic, "JAL") == 0) return JAL;
    if (strcmp(mnemonic, "BEQ") == 0) return BEQ;
    if (strcmp(mnemonic, "BNE") == 0) return BNE;
    if (strcmp(mnemonic, "BLT") == 0) return BLT;
    if (strcmp(mnemonic, "BGT") == 0) return BGT;
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
        case SW:  return "SW";
        case J:   return "J";
        case JAL: return "JAL";
        case BEQ: return "BEQ";
        case BNE: return "BNE";
        case BLT: return "BLT";
        case BGT: return "BGT";
        case HALT: return "HALT";
        default: return "???";
    }
}

int latency_for_op(OpType op) {
    switch (op) {
        case LI:  return 1;
        case SW:  return 1;
        case ADD: return 1;
        case SUB: return 1;
        case MUL: return 2;
        case DIV: return 2;
        case J:   return 1;
        case JAL: return 1;
        case BEQ: return 1;
        case BNE: return 1;
        case BLT: return 1;
        case BGT: return 1;
        default:  return 1;
    }
}

// Função auxiliar para buscar label
int buscar_label(const char *nome) {
    for (int i = 0; i < num_labels; i++) {
        if (strcmp(tabela_labels[i].nome, nome) == 0) {
            return tabela_labels[i].endereco;
        }
    }
    return -1;
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
    
    // HALT precisa entrar no ROB para ser commitado
    if (instr_atual.op == HALT) {
        if (rob_cheio()) {
            printf("Stall: ROB cheio (aguardando para HALT).\n");
            return;
        }
        // Aloca entrada no ROB para HALT
        int rob_idx = cpu_core.rob_tail;
        fila_reordenacao[rob_idx].op = HALT;
        fila_reordenacao[rob_idx].reg_arq_dest = -1;
        fila_reordenacao[rob_idx].pronto = true; // HALT está pronto imediatamente
        fila_reordenacao[rob_idx].em_uso = true;
        fila_reordenacao[rob_idx].pc_origem = cpu_core.pc;
        fila_reordenacao[rob_idx].target_pc = -1;
        fila_reordenacao[rob_idx].branch_taken = false;
        fila_reordenacao[rob_idx].valor = 0;
        
        cpu_core.rob_tail = (cpu_core.rob_tail + 1) % TAM_FILA_ROB;
        cpu_core.rob_contagem++;
        
        printf("Issue: PC=%d -> ROB[%d], HALT\n", cpu_core.pc, rob_idx);
        cpu_core.pc++;
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
    // SW não escreve em registrador, então rd = -1
    fila_reordenacao[rob_idx].reg_arq_dest = (instr_atual.op == SW) ? -1 : instr_atual.rd;
    fila_reordenacao[rob_idx].pronto = false;
    fila_reordenacao[rob_idx].em_uso = true;
    fila_reordenacao[rob_idx].pc_origem = cpu_core.pc;
    fila_reordenacao[rob_idx].target_pc = -1;
    fila_reordenacao[rob_idx].branch_taken = false;

    cpu_core.rob_tail = (cpu_core.rob_tail + 1) % TAM_FILA_ROB;
    cpu_core.rob_contagem++;
    
    estacoes_reserva[er_idx].ocupado = true;
    estacoes_reserva[er_idx].op = instr_atual.op;
    estacoes_reserva[er_idx].rob_destino = rob_idx;
    estacoes_reserva[er_idx].cycles_left = 0;
    estacoes_reserva[er_idx].pc_origem = cpu_core.pc;
    estacoes_reserva[er_idx].target_pc = -1;
    
    // Busca operandos
    if (instr_atual.op == LI) {
        // LW rd, rs1, offset - precisa do valor de rs1 e do offset
        // val_j = valor de rs1 (base address)
        // val_k = offset (immediate)
        estacoes_reserva[er_idx].tag_j = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs1 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_j = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_j == -1)
            estacoes_reserva[er_idx].val_j = registradores_arq.regs[instr_atual.rs1];
        
        estacoes_reserva[er_idx].tag_k = -1;
        estacoes_reserva[er_idx].val_k = instr_atual.rs2; // offset (immediate)
    } else if (instr_atual.op == SW) {
        // SW rs2, offset(rs1) - precisa do valor de rs1 (base), rs2 (dado) e offset
        // val_j = valor de rs1 (base address)
        // val_k = valor de rs2 (dado a ser armazenado)
        // offset = immediate (armazenado em instr_atual.rs2, mas vamos usar immediate)
        estacoes_reserva[er_idx].tag_j = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs1 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_j = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_j == -1)
            estacoes_reserva[er_idx].val_j = registradores_arq.regs[instr_atual.rs1];
        
        // Busca valor de rs2 (dado a ser armazenado)
        estacoes_reserva[er_idx].tag_k = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs2 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_k = i; // Depende do ROB[i]
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_k == -1)
            estacoes_reserva[er_idx].val_k = registradores_arq.regs[instr_atual.rs2];
        
        // Armazena offset no target_pc (reutilizando campo)
        // Para SW, o offset está em immediate (será corrigido no parser)
        estacoes_reserva[er_idx].target_pc = instr_atual.immediate; // offset armazenado aqui temporariamente
    } else if (instr_atual.op == J) {
        // Jump incondicional - não precisa de operandos
        estacoes_reserva[er_idx].tag_j = -1;
        estacoes_reserva[er_idx].val_j = instr_atual.immediate; // Endereço de destino
        estacoes_reserva[er_idx].tag_k = -1;
        estacoes_reserva[er_idx].val_k = 0;
    } else if (instr_atual.op == JAL) {
        // JAL - salva PC+1 em rd
        estacoes_reserva[er_idx].tag_j = -1;
        estacoes_reserva[er_idx].val_j = instr_atual.immediate; // Endereço de destino
        estacoes_reserva[er_idx].val_k = cpu_core.pc + 1; // PC+1 para salvar em rd
        estacoes_reserva[er_idx].tag_k = -1;
    } else if (instr_atual.op == BEQ || instr_atual.op == BNE || 
               instr_atual.op == BLT || instr_atual.op == BGT) {
        // Branches condicionais - precisam de rs1 e rs2
        estacoes_reserva[er_idx].tag_j = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs1 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_j = i;
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_j == -1)
            estacoes_reserva[er_idx].val_j = registradores_arq.regs[instr_atual.rs1];
        
        estacoes_reserva[er_idx].tag_k = -1;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].reg_arq_dest == instr_atual.rs2 && !fila_reordenacao[i].pronto) {
                estacoes_reserva[er_idx].tag_k = i;
                break;
            }
        }
        if (estacoes_reserva[er_idx].tag_k == -1)
            estacoes_reserva[er_idx].val_k = registradores_arq.regs[instr_atual.rs2];
        
        // Armazena endereço de destino no target_pc
        estacoes_reserva[er_idx].target_pc = instr_atual.immediate;
        // Para branches, val_k contém rs2, mas precisamos preservar isso
        // O target_pc já foi armazenado acima
    } else {
        // Operações aritméticas normais
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
    
    // Impressão de Issue
    if (instr_atual.op == LI) {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], LW R%d, R%d, %d\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.rd, instr_atual.rs1, instr_atual.rs2);
    } else if (instr_atual.op == SW) {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], SW R%d, %d(R%d)\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.rs2, instr_atual.immediate, instr_atual.rs1);
    } else if (instr_atual.op == J) {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], J %d\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.immediate);
    } else if (instr_atual.op == JAL) {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], JAL R%d, %d\n",
           cpu_core.pc, er_idx, rob_idx, instr_atual.rd, instr_atual.immediate);
    } else if (instr_atual.op == BEQ || instr_atual.op == BNE || 
               instr_atual.op == BLT || instr_atual.op == BGT) {
        printf("Issue: PC=%d -> ER[%d], ROB[%d], %s R%d, R%d, %d\n",
           cpu_core.pc, er_idx, rob_idx, nome_operacao(instr_atual.op), 
           instr_atual.rs1, instr_atual.rs2, instr_atual.immediate);
    } else {
        const char *op_str = (instr_atual.op == ADD) ? "+" : (instr_atual.op == SUB) ? "-" : 
                            (instr_atual.op == MUL) ? "*" : (instr_atual.op == DIV) ? "/" : "?";
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
        
        // Para branches condicionais, LW e SW, precisa esperar operandos
        bool precisa_operandos = (unidade->op == BEQ || unidade->op == BNE || 
                                   unidade->op == BLT || unidade->op == BGT ||
                                   unidade->op == LI || unidade->op == SW);
        
        // Se ainda esperando operandos, não começa a contagem
        if (precisa_operandos && (unidade->tag_j != -1 || unidade->tag_k != -1)) {
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
            bool branch_taken = false;
            
            switch (unidade->op) {
                case ADD: 
                    resultado = unidade->val_j + unidade->val_k; 
                    break;
                case SUB: 
                    resultado = unidade->val_j - unidade->val_k; 
                    break;
                case MUL: 
                    resultado = unidade->val_j * unidade->val_k; 
                    break;
                case DIV: 
                    resultado = (unidade->val_k ? unidade->val_j / unidade->val_k : 0); 
                    break;
                case LI:  
                    // LW: endereco_efetivo = val_j (base) + val_k (offset)
                    // Carrega da memória no endereço efetivo
                    {
                        int endereco_efetivo = unidade->val_j + unidade->val_k;
                        if (endereco_efetivo >= 0 && endereco_efetivo < TAM_MEMORIA_DADOS) {
                            resultado = memoria_dados[endereco_efetivo];
                        } else {
                            fprintf(stderr, "Erro: endereco de memoria invalido: %d\n", endereco_efetivo);
                            resultado = 0;
                        }
                    }
                    break;
                case SW:
                    // SW: endereco_efetivo = val_j (base) + target_pc (offset)
                    // Armazena val_k (rs2) na memória no endereço efetivo
                    {
                        int endereco_efetivo = unidade->val_j + unidade->target_pc;
                        if (endereco_efetivo >= 0 && endereco_efetivo < TAM_MEMORIA_DADOS) {
                            // Armazena o valor na memória (será commitado depois)
                            // Por enquanto, apenas calcula o endereço
                            resultado = endereco_efetivo; // Endereço onde será armazenado
                        } else {
                            fprintf(stderr, "Erro: endereco de memoria invalido: %d\n", endereco_efetivo);
                            resultado = -1;
                        }
                    }
                    break;
                case J:
                    // Jump incondicional - sempre tomado
                    resultado = unidade->val_j; // Endereço de destino
                    branch_taken = true;
                    break;
                case JAL:
                    // JAL - salva PC+1 em rd, pula para destino
                    resultado = unidade->val_k; // PC+1
                    branch_taken = true;
                    break;
                case BEQ:
                    branch_taken = (unidade->val_j == unidade->val_k);
                    resultado = branch_taken ? unidade->target_pc : unidade->pc_origem + 1;
                    break;
                case BNE:
                    branch_taken = (unidade->val_j != unidade->val_k);
                    resultado = branch_taken ? unidade->target_pc : unidade->pc_origem + 1;
                    break;
                case BLT:
                    branch_taken = (unidade->val_j < unidade->val_k);
                    resultado = branch_taken ? unidade->target_pc : unidade->pc_origem + 1;
                    break;
                case BGT:
                    branch_taken = (unidade->val_j > unidade->val_k);
                    resultado = branch_taken ? unidade->target_pc : unidade->pc_origem + 1;
                    break;
                default: 
                    break;
            }
            
            fila_reordenacao[unidade->rob_destino].valor = resultado;
            fila_reordenacao[unidade->rob_destino].pronto = true;
            
            // Para SW, armazena o valor a ser escrito e o endereço
            if (unidade->op == SW) {
                // valor = endereço efetivo (já calculado em resultado)
                // target_pc = valor de rs2 (dado a ser armazenado)
                fila_reordenacao[unidade->rob_destino].target_pc = unidade->val_k; // valor de rs2
            }
            // Para desvios, armazena informações adicionais
            else if (unidade->op == J || unidade->op == JAL || 
                unidade->op == BEQ || unidade->op == BNE || 
                unidade->op == BLT || unidade->op == BGT) {
                fila_reordenacao[unidade->rob_destino].target_pc = unidade->target_pc >= 0 ? unidade->target_pc : unidade->val_j;
                fila_reordenacao[unidade->rob_destino].branch_taken = branch_taken;
            }
            
            if (unidade->op == J || unidade->op == JAL) {
                printf("Execute: ER[%d] (%s) -> ROB[%d] (Target: %d)\n",
                       i, nome_operacao(unidade->op), unidade->rob_destino, resultado);
            } else if (unidade->op == BEQ || unidade->op == BNE || 
                       unidade->op == BLT || unidade->op == BGT) {
                printf("Execute: ER[%d] (%s) -> ROB[%d] (Taken: %s, Target: %d)\n",
                       i, nome_operacao(unidade->op), unidade->rob_destino, 
                       branch_taken ? "Sim" : "Nao", resultado);
            } else if (unidade->op == LI) {
                int endereco_efetivo = unidade->val_j + unidade->val_k;
                printf("Execute: ER[%d] (%s) -> ROB[%d] (Endereco: %d, Valor: %d)\n",
                       i, nome_operacao(unidade->op), unidade->rob_destino, endereco_efetivo, resultado);
            } else if (unidade->op == SW) {
                int endereco_efetivo = unidade->val_j + unidade->target_pc;
                printf("Execute: ER[%d] (%s) -> ROB[%d] (Endereco: %d, Valor: %d)\n",
                       i, nome_operacao(unidade->op), unidade->rob_destino, endereco_efetivo, unidade->val_k);
            } else {
                printf("Execute: ER[%d] (%s) -> ROB[%d] (Resultado: %d)\n",
                       i, nome_operacao(unidade->op), unidade->rob_destino, resultado);
            }
            
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

// Função para fazer flush do pipeline (limpar instruções após branch)
void flush_pipeline(int pc_limite) {
    int flushed_count = 0;
    // Limpa todas as entradas do ROB que são posteriores ao PC limite
    for (int i = 0; i < TAM_FILA_ROB; i++) {
        if (fila_reordenacao[i].em_uso && fila_reordenacao[i].pc_origem > pc_limite) {
            // Libera entrada do ROB
            fila_reordenacao[i].em_uso = false;
            fila_reordenacao[i].pronto = false;
            flushed_count++;
            
            // Libera estação de reserva associada
            for (int j = 0; j < QTD_ESTACOES; j++) {
                if (estacoes_reserva[j].ocupado && estacoes_reserva[j].rob_destino == i) {
                    estacoes_reserva[j].ocupado = false;
                    estacoes_reserva[j].tag_j = estacoes_reserva[j].tag_k = -1;
                    estacoes_reserva[j].val_j = estacoes_reserva[j].val_k = 0;
                    estacoes_reserva[j].cycles_left = 0;
                    break;
                }
            }
        }
    }
    cpu_core.rob_contagem -= flushed_count;
}

// Variável global para indicar que HALT foi commitado
bool halt_commitado = false;

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
        
        OpType op = fila_reordenacao[head_idx].op;
        int dest_reg = fila_reordenacao[head_idx].reg_arq_dest;
        int val_final = fila_reordenacao[head_idx].valor;
        int pc_origem = fila_reordenacao[head_idx].pc_origem;
        bool branch_taken = fila_reordenacao[head_idx].branch_taken;
        int target_pc = fila_reordenacao[head_idx].target_pc;

        // Tratamento de SW (Store Word)
        if (op == SW) {
            // val_final = endereço efetivo
            // target_pc = valor de rs2 (dado a ser armazenado)
            int endereco_efetivo = val_final;
            int valor_armazenar = target_pc;
            if (endereco_efetivo >= 0 && endereco_efetivo < TAM_MEMORIA_DADOS) {
                memoria_dados[endereco_efetivo] = valor_armazenar;
                printf("Commit: SW (ROB[%d]) -> Mem[%d] = %d\n", head_idx, endereco_efetivo, valor_armazenar);
            } else {
                fprintf(stderr, "Erro: endereco de memoria invalido no commit: %d\n", endereco_efetivo);
            }
        }
        // Tratamento de HALT
        else if (op == HALT) {
            printf("Commit: HALT (ROB[%d]) -> Simulacao finalizada\n", head_idx);
            // HALT commitado - marca para parar a simulação
            halt_commitado = true;
        }
        // Tratamento de desvios
        else if (op == J || op == JAL || op == BEQ || op == BNE || op == BLT || op == BGT) {
            if (branch_taken) {
                // Desvio foi tomado - atualiza PC e faz flush
                // Valida se o target_pc é válido
                int novo_pc = target_pc;
                if (novo_pc < 0 || novo_pc >= total_instrucoes) {
                    fprintf(stderr, "Erro: endereco de desvio invalido: %d (max: %d)\n", novo_pc, total_instrucoes - 1);
                    novo_pc = pc_origem + 1; // Fallback: continua sequencialmente
                    if (novo_pc >= total_instrucoes) {
                        novo_pc = total_instrucoes - 1; // Limita ao último endereço válido
                    }
                }
                cpu_core.pc = novo_pc;
                flush_pipeline(pc_origem);
                printf("Commit: %s (ROB[%d]) -> PC = %d (Flush executado)\n", 
                       nome_operacao(op), head_idx, novo_pc);
            } else {
                // Desvio não foi tomado - continua normalmente
                printf("Commit: %s (ROB[%d]) -> PC continua (Branch not taken)\n", 
                       nome_operacao(op), head_idx);
            }
            
            // Para JAL, salva PC+1 no registrador de destino
            if (op == JAL) {
                registradores_arq.regs[dest_reg] = val_final;
                printf("Commit: R%d <- %d (PC+1 do JAL)\n", dest_reg, val_final);
            }
        } else {
            // Instruções normais - atualiza registrador
            if (dest_reg >= 0 && dest_reg < QTD_REGISTRADORES) {
                registradores_arq.regs[dest_reg] = val_final;
                printf("Commit: R%d <- %d (ROB[%d])\n", dest_reg, val_final, head_idx);
            }
        }

        // Libera entrada do ROB
        fila_reordenacao[head_idx].em_uso = false;
        fila_reordenacao[head_idx].pronto = false; 

        cpu_core.rob_head = (cpu_core.rob_head + 1) % TAM_FILA_ROB;
        cpu_core.rob_contagem--;
    }
}

// Main

int main() {
    // Inicializa memória de dados com zeros
    for (int i = 0; i < TAM_MEMORIA_DADOS; i++) {
        memoria_dados[i] = 0;
    }
    
    // Exemplo: inicializa alguns valores na memória para teste
    // O usuário pode modificar isso conforme necessário
    // memoria_dados[0] = 10;
    // memoria_dados[1] = 20;
    // memoria_dados[2] = 30;
    
    FILE *fp = fopen("exemplo_novo.txt", "r");
    if (fp == NULL) {
        perror("Erro ao abrir 'exemplo_novo.txt'");
        return 1;
    }

    // Primeira passada: identificar labels
    char line[200];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp) && line_num < MAX_INSTR_MEM * 2) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';
        
        // Remove espaços iniciais
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == ';' || *p == '#') continue;
        
        // Verifica se é um label (termina com ':')
        char *colon = strchr(p, ':');
        if (colon != NULL) {
            *colon = '\0';
            // Remove espaços do nome do label
            char *label_name = p;
            while (*label_name == ' ' || *label_name == '\t') label_name++;
            if (strlen(label_name) > 0 && num_labels < MAX_LABELS) {
                strncpy(tabela_labels[num_labels].nome, label_name, MAX_LABEL_NAME - 1);
                tabela_labels[num_labels].nome[MAX_LABEL_NAME - 1] = '\0';
                // O endereço será definido na segunda passada
                num_labels++;
            }
        }
        line_num++;
    }
    
    // Segunda passada: carregar instruções
    rewind(fp);
    int instr_count = 0;
    line_num = 0;
    
    while (fgets(line, sizeof(line), fp) && instr_count < MAX_INSTR_MEM) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';
        
        // Remove espaços iniciais
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == ';' || *p == '#') continue;
        
        // Verifica se é um label (termina com ':')
        char *colon = strchr(p, ':');
        if (colon != NULL) {
            // É um label - atualiza endereço
            *colon = '\0';
            char *label_name = p;
            while (*label_name == ' ' || *label_name == '\t') label_name++;
            for (int i = 0; i < num_labels; i++) {
                if (strcmp(tabela_labels[i].nome, label_name) == 0) {
                    tabela_labels[i].endereco = instr_count;
                    break;
                }
            }
            // Pula para próxima linha
            continue;
        }
        
        // Processa instrução
        Operacao instr;
        memset(&instr, 0, sizeof(Operacao));
        instr.immediate = -1;
        instr.label_name[0] = '\0';
        
        char mnemonic[20];
        int rd = -1, rs = -1, rt = -1, imm = 0;
        char label_target[50];
        
        // Tenta ler mnemonic
        if (sscanf(p, "%19s", mnemonic) != 1) continue;
        
        instr.op = decodificar_mnemonico(mnemonic);
        
        // HALT
        if (instr.op == HALT) {
            instr.rd = instr.rs1 = instr.rs2 = 0;
            memoria_instrucoes[instr_count++] = instr;
            break;
        }
        // LW rd, rs1, offset (Load Word: carrega da memória[rs1 + offset] para rd)
        else if (instr.op == LI) {
            if (sscanf(p, "%*s R%d , R%d , %d", &rd, &rs, &imm) != 3 &&
                sscanf(p, "%*s R%d, R%d, %d", &rd, &rs, &imm) != 3) {
                fprintf(stderr, "Erro ao ler LW (linha %d): %s\n", line_num+1, line);
                fprintf(stderr, "Formato esperado: LW R<destino>, R<base>, <offset>\n");
                continue;
            }
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = imm; // rs2 armazena o offset
        }
        // SW rs2, offset(rs1) (Store Word: armazena rs2 na memória[rs1 + offset])
        // Formato: SW R<fonte>, <offset>(R<base>) ou SW R<fonte>, R<base>, <offset>
        else if (instr.op == SW) {
            // Tenta formato offset(rs1) primeiro
            if (sscanf(p, "%*s R%d , %d ( R%d )", &rt, &imm, &rs) == 3 ||
                sscanf(p, "%*s R%d, %d(R%d)", &rt, &imm, &rs) == 3 ||
                sscanf(p, "%*s R%d, %d (R%d)", &rt, &imm, &rs) == 3) {
                instr.rs1 = rs; instr.rs2 = rt; instr.rd = -1; instr.immediate = imm;
            }
            // Tenta formato alternativo: SW R<fonte>, R<base>, <offset>
            else if (sscanf(p, "%*s R%d , R%d , %d", &rt, &rs, &imm) == 3 ||
                     sscanf(p, "%*s R%d, R%d, %d", &rt, &rs, &imm) == 3) {
                instr.rs1 = rs; instr.rs2 = rt; instr.rd = -1; instr.immediate = imm;
            } else {
                fprintf(stderr, "Erro ao ler SW (linha %d): %s\n", line_num+1, line);
                fprintf(stderr, "Formato esperado: SW R<fonte>, <offset>(R<base>) ou SW R<fonte>, R<base>, <offset>\n");
                continue;
            }
        }
        // J label ou J imm
        else if (instr.op == J) {
            if (sscanf(p, "%*s %49s", label_target) == 1) {
                // Tenta como label primeiro
                int label_addr = buscar_label(label_target);
                if (label_addr >= 0) {
                    instr.immediate = label_addr;
                    strncpy(instr.label_name, label_target, MAX_LABEL_NAME - 1);
                } else {
                    // Tenta como número
                    if (sscanf(label_target, "%d", &imm) == 1) {
                        instr.immediate = imm;
                    } else {
                        fprintf(stderr, "Erro: label ou endereco invalido em J (linha %d): %s\n", line_num+1, line);
                        continue;
                    }
                }
            } else {
                fprintf(stderr, "Erro ao ler J (linha %d): %s\n", line_num+1, line);
                continue;
            }
            instr.rd = instr.rs1 = instr.rs2 = -1;
        }
        // JAL R1, label ou JAL R1, imm
        else if (instr.op == JAL) {
            if (sscanf(p, "%*s R%d , %49s", &rd, label_target) == 2 ||
                sscanf(p, "%*s R%d, %49s", &rd, label_target) == 2) {
                int label_addr = buscar_label(label_target);
                if (label_addr >= 0) {
                    instr.immediate = label_addr;
                    strncpy(instr.label_name, label_target, MAX_LABEL_NAME - 1);
                } else {
                    if (sscanf(label_target, "%d", &imm) == 1) {
                        instr.immediate = imm;
                    } else {
                        fprintf(stderr, "Erro: label ou endereco invalido em JAL (linha %d): %s\n", line_num+1, line);
                        continue;
                    }
                }
            } else {
                fprintf(stderr, "Erro ao ler JAL (linha %d): %s\n", line_num+1, line);
                continue;
            }
            instr.rd = rd; instr.rs1 = instr.rs2 = -1;
        }
        // BEQ/BNE/BLT/BGT R1, R2, label ou BEQ/BNE/BLT/BGT R1, R2, imm
        else if (instr.op == BEQ || instr.op == BNE || instr.op == BLT || instr.op == BGT) {
            if (sscanf(p, "%*s R%d , R%d , %49s", &rs, &rt, label_target) == 3 ||
                sscanf(p, "%*s R%d, R%d, %49s", &rs, &rt, label_target) == 3) {
                int label_addr = buscar_label(label_target);
                if (label_addr >= 0) {
                    instr.immediate = label_addr;
                    strncpy(instr.label_name, label_target, MAX_LABEL_NAME - 1);
                } else {
                    if (sscanf(label_target, "%d", &imm) == 1) {
                        instr.immediate = imm;
                    } else {
                        fprintf(stderr, "Erro: label ou endereco invalido em %s (linha %d): %s\n", 
                                nome_operacao(instr.op), line_num+1, line);
                        continue;
                    }
                }
            } else {
                fprintf(stderr, "Erro ao ler %s (linha %d): %s\n", nome_operacao(instr.op), line_num+1, line);
                continue;
            }
            instr.rs1 = rs; instr.rs2 = rt; instr.rd = -1;
        }
        // Operacoes R-R-R (ADD, SUB, MUL, DIV)
        else {
            if (sscanf(p, "%*s R%d , R%d , R%d", &rd, &rs, &rt) != 3 &&
                sscanf(p, "%*s R%d, R%d, R%d", &rd, &rs, &rt) != 3) {
                fprintf(stderr, "Erro ao ler instrucao (linha %d): %s\n", line_num+1, line);
                continue;
            }
            instr.rd = rd; instr.rs1 = rs; instr.rs2 = rt;
        }

        // Validação de registradores
        if (instr.rd >= 0 && (instr.rd < 0 || instr.rd >= QTD_REGISTRADORES)) {
            fprintf(stderr, "Erro: registrador rd fora do intervalo (linha %d): %s\n", line_num+1, line);
            fclose(fp);
            return 1;
        }
        if (instr.rs1 >= 0 && (instr.rs1 < 0 || instr.rs1 >= QTD_REGISTRADORES)) {
            fprintf(stderr, "Erro: registrador rs1 fora do intervalo (linha %d): %s\n", line_num+1, line);
            fclose(fp);
            return 1;
        }
        if (instr.rs2 >= 0 && (instr.rs2 < 0 || instr.rs2 >= QTD_REGISTRADORES)) {
            fprintf(stderr, "Erro: registrador rs2 fora do intervalo (linha %d): %s\n", line_num+1, line);
            fclose(fp);
            return 1;
        }
        
        printf("Instrucao lida [%d]: %s", instr_count, mnemonic);
        if (instr.rd >= 0) printf(" rd=R%d", instr.rd);
        if (instr.rs1 >= 0) printf(" rs1=R%d", instr.rs1);
        if (instr.rs2 >= 0) printf(" rs2=R%d", instr.rs2);
        if (instr.immediate >= 0) printf(" target=%d", instr.immediate);
        if (instr.label_name[0] != '\0') printf(" (label: %s)", instr.label_name);
        printf("\n");

        memoria_instrucoes[instr_count++] = instr;
        line_num++;
    }
    fclose(fp);
    
    // Armazena o número total de instruções
    total_instrucoes = instr_count;

    // Loop principal da simulação
    halt_commitado = false;
    int ciclos_sem_progresso = 0;
    int ultimo_rob_contagem = 0;
    
    while (true) {
        
        // Proteção contra loop infinito
        if (cpu_core.ciclo > 100) {
            printf("Simulacao excedeu 100 ciclos. Abortando.\n");
            break;
        }
        
        // Verifica se há progresso (ROB mudou ou HALT foi commitado)
        if (cpu_core.rob_contagem != ultimo_rob_contagem || halt_commitado) {
            ciclos_sem_progresso = 0;
            ultimo_rob_contagem = cpu_core.rob_contagem;
        } else {
            ciclos_sem_progresso++;
            // Se não há progresso por muitos ciclos e ROB está vazio, pode ser deadlock
            if (ciclos_sem_progresso > 10 && cpu_core.rob_contagem == 0) {
                printf("Nenhum progresso detectado. Parando simulacao.\n");
                break;
            }
        }
        
        // Se HALT foi commitado e ROB está vazio, para
        if (halt_commitado && cpu_core.rob_contagem == 0) {
            break;
        }
        
        // Se não há mais instruções para despachar e ROB está vazio, para
        if (cpu_core.pc >= instr_count && cpu_core.rob_contagem == 0) {
            printf("Fim da memoria de instrucoes alcancado.\n");
            break;
        }
        
        // Verifica se HALT está no ROB (mesmo que não seja o head ainda)
        bool halt_no_rob = false;
        for (int i = 0; i < TAM_FILA_ROB; i++) {
            if (fila_reordenacao[i].em_uso && fila_reordenacao[i].op == HALT) {
                halt_no_rob = true;
                break;
            }
        }
        
        // Se HALT está no ROB e está pronto, deve ser commitado no próximo ciclo
        // Se HALT está no ROB e ROB está vazio (tudo foi commitado), para
        if (halt_no_rob && cpu_core.rob_contagem == 0) {
            break;
        }
        
        // Se não há mais instruções para despachar, PC está fora do range, e ROB está vazio, para
        if (cpu_core.pc >= instr_count && cpu_core.rob_contagem == 0 && !halt_no_rob) {
            printf("Fim da memoria de instrucoes alcancado (sem HALT).\n");
            break;
        }

        printf("Ciclo %d\n", cpu_core.ciclo);
        mostrar_banco_regs();
        mostrar_estacoes_reserva();

        // Despacha instruções se HALT ainda não foi commitado e PC é válido
        if (!halt_commitado && cpu_core.pc < instr_count) {
            etapa_despacho(instr_count);
        }
        etapa_execucao();
        etapa_finalizacao();

        cpu_core.ciclo++;
        printf("\n");
    }

    printf("ESTADO FINAL\n");
    mostrar_regs_final();

    return 0;
}