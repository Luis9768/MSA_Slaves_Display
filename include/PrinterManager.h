#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include "Common.h"
#include <Arduino.h>

void setupPrinter();
void imprimirEtiqueta(Receita r, int contador, DataProducao data, int re);

#endif
