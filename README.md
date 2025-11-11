# README: Simulador do Algoritmo de Tomasulo

Este projeto é um simulador em C do algoritmo de Tomasulo, implementando escalonamento dinâmico e execução fora de ordem (out-of-order) com suporte a dependências de dados, estações de reserva e Buffer de Reordenação (ROB).

## 1. O Algoritmo de Tomasulo

Baseado nos conceitos do livro *Computer Architecture: A Quantitative Approach* (Hennessy & Patterson), o algoritmo de Tomasulo é uma técnica de escalonamento dinâmico que permite execução paralela e especulativa de instruções, maximizando o paralelismo de instruções (ILP).

Neste simulador, há suporte a execução fora de ordem com controle de dependências de dados via estações de reserva (ER) e reordenação in-order no commit, garantindo a consistência do estado arquitetural.

### Estágios do Pipeline

1. **Issue (Emissão):** A instrução é alocada em uma Estação de Reserva (ER) e em um slot do Buffer de Reordenação (ROB). A ER aguarda os operandos ficarem disponíveis.
2. **Execute (Execução):** Quando os operandos estão prontos, a instrução é executada. O resultado é gravado no ROB e marcado como pronto.
3. **Write/Commit (Escrita e Efetivação):** O resultado pronto é difundido no barramento CDB, permitindo que outras ERs recebam os valores. O commit em ordem atualiza o banco de registradores, garantindo consistência e exceções precisas.

O commit em ordem assegura que o processador mantenha um estado previsível mesmo com execução fora de ordem.

---

## 2. Integrantes do Grupo

* **Professor:** Matheus Alcântara Souza
* Caroline Freitas Alvernaz
* Giovanna Naves Ribeiro
* Júlia Rodrigues Vasconcellos Melo
* Marcos Paulo da Silva Laine
* Priscila Andrade de Moraes

---

## 3. Como Compilar e Executar

O projeto foi escrito em C e pode ser compilado com `gcc`.
O simulador lê as instruções de um arquivo chamado `simulacao.txt`.

**Compilação:**

```bash
gcc -o tomasulo tomasulo.c
```

**Execução:**

```bash
./tomasulo
```

---

## 4. Formato do Arquivo de Entrada (`simulacao.txt`)

O arquivo `simulacao.txt` deve conter uma instrução por linha, com mnemônicos em letras maiúsculas e registradores prefixados com `R`.

### Operações Suportadas

* **LW (Load Word com base e deslocamento):**
  Calcula o endereço base + offset e grava o valor resultante no registrador de destino.
  (Neste simulador, não há memória de dados; o resultado é tratado como o valor base + offset.)

  ```
  LW R1, R0 (10)
  ```

  Exemplo: R1 ← R0 + 10

* **ADD (Adição):**
  Soma dois registradores.

  ```
  ADD R3, R1, R2
  ```

  Exemplo: R3 ← R1 + R2

* **SUB (Subtração):**
  Subtrai dois registradores.

  ```
  SUB R4, R2, R1
  ```

  Exemplo: R4 ← R2 − R1

* **MUL (Multiplicação):**
  Multiplica dois registradores.

  ```
  MUL R5, R1, R2
  ```

  Exemplo: R5 ← R1 × R2
  (Latência de 2 ciclos)

  * **LW :** Carrega um valor de outro registrador + offset
      * `LW R1, R2 (8)`
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
LW R1, R0 (10)
LW R2, R0 (20)
ADD R3, R1, R2
MUL R4, R3, R1
SUB R5, R4, R2
HALT
```

**Resultado esperado:**

* R1 = 10
* R2 = 20
* R3 = 30
* R4 = 300
* R5 = 280

---

## 5. Siglas e Conceitos-Chave

* **ER / RS (Estação de Reserva):**
  Armazena instruções emitidas e operandos (ou referências de quem os produzirá).
  Permite renomeação de registradores, eliminando conflitos WAR e WAW.

* **ROB (Buffer de Reordenação):**
  Armazena resultados prontos de instruções executadas.
  Garante o commit (efetivação) em ordem, mantendo o estado arquitetural consistente.

* **CDB (Common Data Bus):**
  Barramento de difusão dos resultados prontos (valores do ROB ou das UFs) para todas as ERs.

* **RAW (Read After Write):**
  Dependência verdadeira. A instrução espera o valor ser produzido antes de ler.

* **WAR (Write After Read):**
  Conflito falso. Resolvido copiando o valor lido para a ER no momento da emissão.

* **WAW (Write After Write):**
  Conflito falso de escrita. Resolvido via renomeação de registradores com o ROB.

* **Commit (Efetivação):**
  Estágio final, no qual a instrução mais antiga e pronta do ROB tem seu resultado escrito no banco de registradores.

---
