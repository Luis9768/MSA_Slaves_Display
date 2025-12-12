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
  Serial.begin(115200);

  // 1. Inicializa Display (LVGL)
  setupDisplay();

  // 2. Inicializa Rede (ESP-NOW)
  setupNetworkSlave();

  // 3. Inicializa Impressora
  setupPrinter();

  Serial.println(">>> SLAVE INICIADO (CAROUSEL + PRINTER) <<<");
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
    Serial.println(">>> UPDATE: Lista atualizada pelo Master! <<<");
    std::vector<Receita> novaLista = getListaReceitas();

    Serial.printf("DEBUG: EstadoAtual=%d, IDProdutoAtual=%d, TamanhoLista=%d\n",
                  estadoAtual, idProdutoAtual, novaLista.size());

    bool produtoAtualExiste = false;

    // Se lista vazia, força saída
    if (novaLista.empty()) {
      Serial.println("DEBUG: Lista VAZIA! Forçando saída.");
      idProdutoAtual = 0;
      estadoAtual = 0;
    }
    // Se estamos em produção, verifica se o produto ainda existe
    else if (estadoAtual == 1 && idProdutoAtual > 0) {
      Serial.println("DEBUG: Verificando se produto atual ainda existe...");
      for (const auto &r : novaLista) {
        if (r.id == idProdutoAtual) {
          produtoAtualExiste = true;
          receitaAtiva = r;
          Serial.println("DEBUG: Produto ENCONTRADO na nova lista.");
          break;
        }
      }

      if (!produtoAtualExiste) {
        Serial.println("DEBUG: Produto NAO ENCONTRADO! Removido! Voltando...");
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
      Serial.println("DEBUG: Atualizando Carousel UI.");
      mostrarCarouselSlave(novaLista, 0);
    }

    confirmarAtualizacaoLista();
  }
  // --------------------------------------------------------------------

  if (estadoAtual == 0) { // ESTADO: CAROUSEL
    if (acao > 0) {
      Serial.printf("Entrando no produto ID %d -> Indo para DATA\n", acao);
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
      Serial.printf("Data Confirmada: %02d/%02d/%02d -> Indo para RE\n",
                    currentDate.dia, currentDate.mes, currentDate.ano);
      mostrarTelaRE();
      estadoAtual = 2;
    }
  } else if (estadoAtual == 2) { // ESTADO: RE
    if (acao == -1) {            // Voltar (para Data)
      mostrarTelaData();
      estadoAtual = 1;
    } else if (acao == -4) { // Confirmou RE
      currentRE = getDadosRE();
      Serial.printf("RE Confirmado: %d -> Indo para PRODUCAO\n", currentRE);
      contadorProducao = 0;
      mostrarTelaProducao(receitaAtiva);
      estadoAtual = 3;
    }
  } else if (estadoAtual == 3) { // ESTADO: PRODUÇÃO
    // Se pediu para voltar (ID -1) - Cancelar Produção
    if (acao == -1) {
      Serial.println("Cancelando producao...");
      estadoAtual = 0;
      idProdutoAtual = 0;
      mostrarCarouselSlave(getListaReceitas(), 0);
    }

    // Nao tem mais navegacao de proximo produto aqui
  } else if (estadoAtual == 4) { // ESTADO: CONCLUIDA (WAIT)
    if (millis() - timeProducaoConcluida > 3000) {
      // Reinicia contagem automaticamente
      Serial.println(">>> Reiniciando contagem automatico <<<");
      contadorProducao = 0;
      esconderMensagemProducaoConcluida();
      atualizarContador(0, receitaAtiva.quantidade);
      estadoAtual = 3; // Volta para produção
    }
  }

  // LOGICA SCANNER GLOBAL (Só ativa no estado 3)
  if (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (estadoAtual == 3) {
        if (contadorProducao >= receitaAtiva.quantidade) {
          Serial.println(">>> JA ESTA CHEIO! <<<");
        } else {
          contadorProducao++;
          atualizarContador(contadorProducao, receitaAtiva.quantidade);
          // Imprime usando Data e RE globais
          imprimirEtiqueta(receitaAtiva, contadorProducao, currentDate,
                           currentRE);

          // Verifica se concluiu
          if (contadorProducao >= receitaAtiva.quantidade) {
            Serial.println(">>> PRODUCAO CONCLUIDA! <<<");
            mostrarMensagemProducaoConcluida();
            timeProducaoConcluida = millis();
            estadoAtual = 4;
          }

          delay(50);
          while (Serial.available())
            Serial.read();
        }
      }
    }
  }

  // Pequeno delay para não fritar a CPU (opcional, mas bom para LVGL)
  delay(5);
}