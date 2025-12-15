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
// Se Debug Habilitado, inicia Serial em alta velocidade para logs
#if DEBUG_ENABLED
  Serial.begin(115200);
#endif

  // 1. Inicializa Display (LVGL)
  setupDisplay();

  // 2. Inicializa Rede (ESP-NOW)
  setupNetworkSlave();

  // 3. Inicializa Impressora
  setupPrinter();

  // 4. Inicializa Sensor
  pinMode(PIN_SENSOR_PRODUTO, INPUT_PULLUP);

  Serial.println(">>> SLAVE INICIADO (CAROUSEL + PRINTER) <<<");
  DBGLN(">>> SLAVE INICIADO (CAROUSEL + PRINTER) <<<");
}

void loop() {
  // Mantém a UI responsiva
  loopDisplay();
  loopNetworkSlave();

  // Lógica de Navegação
  // Lógica de Navegação
  int acao = verificarToque();

  // REMOVIDO: Lógica serial antiga, movida para dentro do Loop de estado

  // --- 1. GLOBAL: Verifica atualizações de lista (Prioridade Máxima) ---
  if (novaListaDisponivel()) {
    DBGLN(">>> UPDATE: Lista atualizada pelo Master! <<<");
    std::vector<Receita> novaLista = getListaReceitas();

    DBGF("DEBUG: EstadoAtual=%d, IDProdutoAtual=%d, TamanhoLista=%d\n",
         estadoAtual, idProdutoAtual, novaLista.size());

    bool produtoAtualExiste = false;

    // Se lista vazia, força saída
    if (novaLista.empty()) {
      DBGLN("DEBUG: Lista VAZIA! Forçando saída.");
      idProdutoAtual = 0;
      estadoAtual = 0;
    }
    // Se estamos em produção, verifica se o produto ainda existe
    else if (estadoAtual == 1 && idProdutoAtual > 0) {
      DBGLN("DEBUG: Verificando se produto atual ainda existe...");
      for (const auto &r : novaLista) {
        if (r.id == idProdutoAtual) {
          produtoAtualExiste = true;
          receitaAtiva = r;
          DBGLN("DEBUG: Produto ENCONTRADO na nova lista.");
          break;
        }
      }

      if (!produtoAtualExiste) {
        DBGLN("DEBUG: Produto NAO ENCONTRADO! Removido! Voltando...");
        idProdutoAtual = 0;
        estadoAtual = 0; // Força volta para carousel
      }
    } else {
      // Se o ID mudou ou algo assim, garantimos que não estamos em ID invalido
      if (estadoAtual == 1 && idProdutoAtual == 0)
        estadoAtual = 0;
    }

    // Atualiza UI se estiver no Carousel (ou foi forçado a voltar)
    if (estadoAtual == 0) {
      DBGLN("DEBUG: Atualizando Carousel UI.");
      mostrarCarouselSlave(novaLista, 0);
    }

    confirmarAtualizacaoLista();
  }
  // --------------------------------------------------------------------

  if (estadoAtual == 0) { // ESTADO: CAROUSEL
    if (acao > 0) {
      DBGF("Entrando no produto ID %d -> Indo para DATA\n", acao);
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
      DBGF("Data Confirmada: %02d/%02d/%02d -> Indo para RE\n", currentDate.dia,
           currentDate.mes, currentDate.ano);
      mostrarTelaRE();
      estadoAtual = 2;
    }
  } else if (estadoAtual == 2) { // ESTADO: RE
    if (acao == -1) {            // Voltar (para Data)
      mostrarTelaData();
      estadoAtual = 1;
    } else if (acao == -4) { // Confirmou RE
      currentRE = getDadosRE();
      DBGF("RE Confirmado: %d -> Indo para PRODUCAO\n", currentRE);
      contadorProducao = 0;
      mostrarTelaProducao(receitaAtiva);
      estadoAtual = 3;
    }
  } else if (estadoAtual == 3) { // ESTADO: PRODUÇÃO
    // Se pediu para voltar (ID -1) - Cancelar Produção
    if (acao == -1) {
      DBGLN("Cancelando producao...");
      estadoAtual = 0;
      idProdutoAtual = 0;
      mostrarCarouselSlave(getListaReceitas(), 0);
    }
    // RESET MANUAL (ID -10)
    else if (acao == -10) {
      DBGLN(">>> RESET MANUAL DE CONTAGEM <<<");
      contadorProducao = 0;
      atualizarContador(0, receitaAtiva.quantidade);
    }

    // Nao tem mais navegacao de proximo produto aqui
  } else if (estadoAtual == 4) { // ESTADO: CONCLUIDA (WAIT)
    if (millis() - timeProducaoConcluida > 3000) {
      // Reinicia contagem automaticamente
      DBGLN(">>> Reiniciando contagem automatico <<<");
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

    if (input.length() > 0) {
// Se debug estiver OFF, não temos como logar o que chegou, mas processamos.
#if DEBUG_ENABLED
      DBGF(">>> SCANNER LEU: [%s] <<<\n", input.c_str());
#endif

      // VISUAL DEBUG: Mostra o que leu na tela
      char debugMsg[64];
      snprintf(debugMsg, sizeof(debugMsg), "Lido: %s", input.c_str());
      atualizarDebugInfo(debugMsg);

      if (estadoAtual == 3) {
        // Validação: É "1" (Teste), é o Codigo ou é o Barcode?
        bool valido = (input == "1") ||
                      (String(receitaAtiva.barcode) == input) ||
                      (String(receitaAtiva.codigo) == input);

        if (valido) {
          if (contadorProducao >= receitaAtiva.quantidade) {
#if DEBUG_ENABLED
            DBGLN(">>> JA ESTA CHEIO! <<<");
#endif
          } else {
            contadorProducao++;
            atualizarContador(contadorProducao, receitaAtiva.quantidade);
#if DEBUG_ENABLED
            DBGF(">>> SUCESSO: %d/%d <<<\n", contadorProducao,
                 receitaAtiva.quantidade);
#endif

            // Imprime Etiqueta
            imprimirEtiqueta(receitaAtiva, contadorProducao, currentDate,
                             currentRE);

            // Verifica se concluiu
            if (contadorProducao >= receitaAtiva.quantidade) {
#if DEBUG_ENABLED
              DBGLN(">>> PRODUCAO CONCLUIDA! <<<");
#endif
              mostrarMensagemProducaoConcluida();
              timeProducaoConcluida = millis();
              estadoAtual = 4;
            }
          }
        } else {
#if DEBUG_ENABLED
          DBGLN(">>> ERRO: CODIGO DE BARRAS INVALIDO PARA ESTE PRODUTO! <<<");
#endif
        }
      } else {
#if DEBUG_ENABLED
        DBGLN(">>> ERRO: NAO ESTA NA TELA DE PRODUCAO <<<");
#endif
      }
    }
  }

  // LOGICA SENSOR FISICO (Simulação ou Sensor Real NPN)
  static int lastSensorState = HIGH;
  int sensorState = digitalRead(PIN_SENSOR_PRODUTO);

  if (estadoAtual == 3 && lastSensorState == HIGH && sensorState == LOW) {
    // Borda de descida detectada (Fio encostou no GND)
    DBGLN(">>> SENSOR FISICO ACIONADO! <<<");

    if (contadorProducao >= receitaAtiva.quantidade) {
      DBGLN(">>> JA ESTA CHEIO! <<<");
    } else {
      contadorProducao++;
      atualizarContador(contadorProducao, receitaAtiva.quantidade);
      DBGF(">>> SUCESSO (SENSOR): %d/%d <<<\n", contadorProducao,
           receitaAtiva.quantidade);

      imprimirEtiqueta(receitaAtiva, contadorProducao, currentDate, currentRE);

      if (contadorProducao >= receitaAtiva.quantidade) {
        mostrarMensagemProducaoConcluida();
        timeProducaoConcluida = millis();
        estadoAtual = 4;
      }
    }
    delay(200); // Debounce basico
  }
  lastSensorState = sensorState;

  // Pequeno delay para não fritar a CPU (opcional, mas bom para LVGL)
  delay(5);
}