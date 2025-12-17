#ifndef COMMON_H
#define COMMON_H
#include <Arduino.h>

struct Receita {
  int id;
  char codigo[16];
  int quantidade;
  char descricao[32];
  char barcode[16];
  bool ativa;
};

// Packet Types
#define PKG_DATA 1       // Envio de Receita Individual
#define PKG_RESET 2      // Comando de Reset Total
#define PKG_HEARTBEAT 3  // Heartbeat do Slave
#define PKG_SYNC_START 4 // Inicio de Sincronizacao em Massa
#define PKG_SYNC_END 5   // Fim de Sincronizacao em Massa

struct PacoteRede {
  int tipo;     // Vide Defines acima
  long version; // Versao Global do Master (Para Sync)
  Receita dados;
};

struct DataProducao {
  int dia;
  int mes;
  int ano;
};
#endif