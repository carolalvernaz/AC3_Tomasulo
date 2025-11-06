# README: Simulador do Algoritmo de Tomasulo

Este projeto é um simulador em C do algoritmo de Tomasulo, implementando escalonamento dinâmico e execução especulativa por meio de um Buffer de Reordenação (ROB).

## 1\. O Algoritmo de Tomasulo com Especulação

As explicações são baseadas nos conceitos do livro *Computer Architecture: A Quantitative Approach*.

O algoritmo de Tomasulo é uma técnica de escalonamento dinâmico para execução fora de ordem (out-of-order) que maximiza o paralelismo (ILP). Este simulador implementa a versão moderna com **execução especulativa**, usando um Buffer de Reordenação (ROB) para permitir que o processador execute instruções de desvios (branches) antes que eles sejam resolvidos.

### O Pipeline de Execução

O pipeline de execução possui os seguintes estágios:

1.  **Issue (Emissão):** A instrução é alocada em uma Estação de Reserva (ER) e em um slot no Buffer de Reordenação (ROB). A ER aguarda os operandos ficarem disponíveis.
2.  **Execute (Execução):** Quando os operandos estão prontos, a instrução é executada. O resultado é escrito no ROB e marcado como 'pronto'.
3.  **Write/Commit (Escrita e Efetivação):** O resultado pronto é difundido no Common Data Bus (CDB) para atualizar outras ERs. A instrução na 'cabeça' do ROB que está pronta é 'comitada' (efetivada), escrevendo seu resultado final no banco de registradores.

O *Commit* em ordem garante que o estado do processador só seja atualizado permanentemente, permitindo a recuperação de especulações erradas e exceções precisas.

## 2\. Integrantes do Grupo

  * **Professor:** Matheus Alcântara Souza
  * Caroline Freitas Alvernaz
  * Giovanna Naves Ribeiro
  * Júlia Rodrigues Vasconcellos Melo
  * Marcos Paulo da Silva Laine
  * Priscila Andrade de Moraes

## 3\. Como Compilar e Executar

O projeto foi escrito em C e pode ser compilado com `gcc`. O simulador lê as instruções do arquivo `simulacao.txt`.

**Compilação:**
(Supondo que o código-fonte se chame `tomasulo.c`)

```bash
gcc -o tomasulo tomasulo.c
```

**Execução:**

```bash
./tomasulo
```

*(O programa procurará automaticamente pelo arquivo `simulacao.txt` no mesmo diretório.)*

## 4\. Formato do Arquivo de Entrada (`simulacao.txt`)

O arquivo `simulacao.txt` deve conter uma instrução por linha, com mnemônicos em MAIÚSCULAS e registradores prefixados com `R`.

**Operações Suportadas:**

  * **LD (Load Immediate):** Carrega um valor imediato.
      * `LD R1, R0, 6`
  * **ADD (Adição):** Soma dois registradores.
      * `ADD R3, R1, R2`
  * **SUB (Subtração):** Subtrai dois registradores.
      * `SUB R5, R3, R4`
  * **MUL (Multiplicação):** Multiplica dois registradores.
      * `MUL R4, R1, R2`
  * **HALT (Parada):** Indica o fim do programa.
      * `HALT`

### Exemplo de `simulacao.txt`

```
LD R1, R0, 6
LD R2, R0, 10
ADD R3, R1, R2
MUL R4, R1, R2
SUB R5, R3, R4
HALT
```

## 5\. Siglas e Conceitos-Chave

  * **ER / RS (Estação de Reserva):** Armazena instruções emitidas e seus operandos (ou tags de quem os produzirá). Permite o **renome de registradores** para eliminar conflitos WAR e WAW.
  * **ROB (Buffer de Reordenação):** Fila que armazena resultados de instruções executadas. Garante que o *commit* (efetivação) seja feito na ordem original do programa.
  * **CDB (Common Data Bus):** Barramento de difusão (broadcast) que entrega resultados prontos do ROB e das UFs para as Estações de Reserva.
  * **RAW (Read-After-Write):** Dependência de dados verdadeira (Leitura Após Escrita). A instrução espera na ER pelo resultado.
  * **WAR (Write-After-Read):** Conflito falso (Escrita Após Leitura). Resolvido copiando o valor lido para a ER no momento da emissão.
  * **WAW (Write-After-Write):** Conflito falso (Escrita Após Escrita). Resolvido pelo renome de registradores (ROB e ER).
  * **Commit (Efetivação):** Estágio final onde o resultado da instrução mais antiga é permanentemente escrito no banco de registradores.
