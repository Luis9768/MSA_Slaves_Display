#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "Common.h"
#include <vector>

void setupDisplay();
void loopDisplay();

// Mostra tela de seleção em Carousel (Card por Card)
void mostrarCarouselSlave(std::vector<Receita> lista, int indice);

// Funções de navegação do Carousel
void proximoProdutoCarousel();
void entrarProdutoCarousel();

// Telas intermerdiárias (Data e RE)
void mostrarTelaData();
void mostrarTelaRE();

// Mostra a tela de contagem/produção (com Progress Bar agora)
void mostrarTelaProducao(Receita r);

// Atualiza o número no contador
void atualizarContador(int qtd, int meta);

// Mostra mensagem de Produção Concluída
void mostrarMensagemProducaoConcluida();

// Esconde mensagem de Produção Concluída
void esconderMensagemProducaoConcluida();

// Verifica toques na tela e retorna o ID do produto clicado (ou -1 se nenhum)
// Se estiver na tela de produção, retorna -2 para "Sair"
// Retorna -3 se confirmou DATA
// Retorna -4 se confirmou RE
int verificarToque();

// Getters para os dados inseridos
DataProducao getDadosData();
int getDadosRE();

#endif