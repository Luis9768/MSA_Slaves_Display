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

struct PacoteRede {
  int tipo; // 1=Receita, 2=Reset, 3=Heartbeat
  Receita dados;
};

struct DataProducao {
  int dia;
  int mes;
  int ano;
};
#endif