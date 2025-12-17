#include "Config.h"
#include "DisplayManager.h"
#include "NetworkSlave.h"
#include "PrinterManager.h"
#include <Arduino.h>

// Estado da Aplicação
// 0 = Lista de Produtos (Carousel)
// 1 = Tela de Data
// 2 = Tela de RE
// 3 = Tela de Produção
// 4 = Produção Concluída (Delay)
int estadoAtual = 0;
int idProdutoAtual = 0;
int contadorProducao = 0;
Receita receitaAtiva; // Armazena a receita atual para impressão

// Dados da Sessão
DataProducao currentDate = {1, 1, 24};
int currentRE = 0;
unsigned long timeProducaoConcluida = 0;

void setup() {
  // 1. Inicializa Display (LVGL)
  setupDisplay();

  // FIX: Start Main Serial for Scanner (Pins 1 & 3)
  Serial.begin(9600);

  // 2. Inicializa Rede (ESP-NOW)
  setupNetworkSlave();

  // 3. Inicializa Impressora
  setupPrinter();

  // 4. Inicializa Sensor
  // 4. Inicializa o Sensor "Oficial"
  // ATIVANDO MODO MULTI-SENSOR (Para descobrir qual pino esta funcionando)
  // Adicionado pino 22 na lista
  int candidatos[] = {22, 34, 35, 26, 14, 12, 13, 4, 5};
  for (int p : candidatos) {
    if (p == 34 || p == 35)
      pinMode(p, INPUT); // 34/35 nao tem pullup interno
    else
      pinMode(p, INPUT_PULLUP);
  }
}

void loop() {
  // Mantém a UI responsiva
  loopDisplay();
  loopNetworkSlave();

  int acao = verificarToque();

  // --- 1. GLOBAL: Verifica atualizações de lista (Prioridade Máxima) ---
  if (novaListaDisponivel()) {
    std::vector<Receita> novaLista = getListaReceitas();

    bool produtoAtualExiste = false;

    // Se lista vazia, força saída
    if (novaLista.empty()) {
      idProdutoAtual = 0;
      estadoAtual = 0;
    }
    // Se estamos em produção, verifica se o produto ainda existe
    else if (estadoAtual == 1 && idProdutoAtual > 0) {
      for (const auto &r : novaLista) {
        if (r.id == idProdutoAtual) {
          produtoAtualExiste = true;
          receitaAtiva = r;
          break;
        }
      }

      if (!produtoAtualExiste) {
        idProdutoAtual = 0;
        estadoAtual = 0; // Força volta para carousel
      }
    } else {
      // Se o ID mudou ou algo assim, garantimos que não estamos em ID
      // invalido
      if (estadoAtual == 1 && idProdutoAtual == 0)
        estadoAtual = 0;
    }

    // Atualiza UI se estiver no Carousel (ou foi forçado a voltar)
    if (estadoAtual == 0) {
      mostrarCarouselSlave(novaLista, 0);
    }

    confirmarAtualizacaoLista();
  }
  // --------------------------------------------------------------------

  if (estadoAtual == 0) { // ESTADO: CAROUSEL
    if (acao > 0) {
      idProdutoAtual = acao;
      // Busca receita
      std::vector<Receita> lista = getListaReceitas();
      for (const auto &r : lista) {
        if (r.id == idProdutoAtual) {
          receitaAtiva = r;
          break;
        }
      }
      mostrarTelaData();
      estadoAtual = 1;
    }
  } else if (estadoAtual == 1) { // ESTADO: DATA
    if (acao == -1) {            // Voltar
      estadoAtual = 0;
      idProdutoAtual = 0;
      mostrarCarouselSlave(getListaReceitas(), 0);
    } else if (acao == -3) { // Confirmou DATA
      currentDate = getDadosData();
      mostrarTelaRE();
      estadoAtual = 2;
    }
  } else if (estadoAtual == 2) { // ESTADO: RE
    if (acao == -1) {            // Voltar (para Data)
      mostrarTelaData();
      estadoAtual = 1;
    } else if (acao == -4) { // Confirmou RE
      currentRE = getDadosRE();
      contadorProducao = 0;
      mostrarTelaProducao(receitaAtiva);
      estadoAtual = 3;
    }
  } else if (estadoAtual == 3) { // ESTADO: PRODUÇÃO
    // Se pediu para voltar (ID -1) - Cancelar Produção
    if (acao == -1) {
      estadoAtual = 0;
      idProdutoAtual = 0;
      mostrarCarouselSlave(getListaReceitas(), 0);
    }
    // RESET MANUAL (ID -10)
    else if (acao == -10) {
      contadorProducao = 0;
      atualizarContador(0, receitaAtiva.quantidade);
    }

    // Nao tem mais navegacao de proximo produto aqui
  } else if (estadoAtual == 4) { // ESTADO: CONCLUIDA (WAIT)
    if (millis() - timeProducaoConcluida > 3000) {
      // Reinicia contagem automaticamente
      contadorProducao = 0;
      esconderMensagemProducaoConcluida();
      atualizarContador(0, receitaAtiva.quantidade);
      estadoAtual = 3; // Volta para produção
    }
  }

  // LOGICA SCANNER GLOBAL (Só ativa no estado 3)
  // [IMPORTANT] Se a impressora estiver no Serial, o Scanner deve estar na
  // mesma velocidade (9600 default)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); // Remove \r \n spaces
    Serial.printf(">>> SCANNER RECEBEU: [%s] <<<\n", input.c_str());

    if (input.length() > 0) {

      if (estadoAtual == 3) {
        // Validação: É "1" (Teste), é o Codigo ou é o Barcode?
        bool valido = (input == "1") ||
                      (String(receitaAtiva.barcode) == input) ||
                      (String(receitaAtiva.codigo) == input);

        if (valido) {
          if (contadorProducao >= receitaAtiva.quantidade) {
            // Ja esta cheio
          } else {
            contadorProducao++;
            atualizarContador(contadorProducao, receitaAtiva.quantidade);

            // Imprime Etiqueta
            imprimirEtiqueta(receitaAtiva, contadorProducao, currentDate,
                             currentRE);

            // Verifica se concluiu
            if (contadorProducao >= receitaAtiva.quantidade) {
              mostrarMensagemProducaoConcluida();
              timeProducaoConcluida = millis();
              estadoAtual = 4;
            }
          }
        } else {
          // Invalido
        }
      } else {
        // Nao esta na tela de producao
      }
    }
  }

  // LOGICA SENSOR MULTIPLO (AUTO-DETECT) - Restaurado
  static int lastStates[40];
  static bool firstRun = true;
  int pinosScan[] = {22, 34, 35, 26, 14, 12, 13, 4, 5};

  if (firstRun) {
    for (int p : pinosScan)
      lastStates[p] = HIGH;
    firstRun = false;
  }

  if (estadoAtual == 3) {
    for (int p : pinosScan) {
      int s = digitalRead(p);
      // Logica INPUT_PULLUP: Ativo em LOW (GND)
      if (lastStates[p] == HIGH && s == LOW) {
        Serial.printf(">>> SENSOR DETECTADO NO PINO %d !!! <<<\n", p);

        if (contadorProducao >= receitaAtiva.quantidade) {
          // Cheio
        } else {
          contadorProducao++;
          atualizarContador(contadorProducao, receitaAtiva.quantidade);
          imprimirEtiqueta(receitaAtiva, contadorProducao, currentDate,
                           currentRE);

          if (contadorProducao >= receitaAtiva.quantidade) {
            mostrarMensagemProducaoConcluida();
            timeProducaoConcluida = millis();
            estadoAtual = 4;
          }
        }
        delay(200); // Debounce
      }
      lastStates[p] = s;
    }
  }

  // Pequeno delay para não fritar a CPU (opcional, mas bom para LVGL)
  delay(5);
}