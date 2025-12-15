#include "PrinterManager.h"
#include "Config.h"

#if !PRINTER_USES_MAIN_SERIAL
HardwareSerial printerSerial(2);
#define PRINTER_OBJ printerSerial
#else
#define PRINTER_OBJ Serial
#endif

void setupPrinter() {
#if PRINTER_USES_MAIN_SERIAL
// Se usa a Serial padrao, ela pode ja ter sido iniciada no setup() se
// DEBUG_ENABLED=1 Mas se DEBUG_ENABLED=0, precisamos iniciar aqui com o
// Baudrate da impressora
#if !DEBUG_ENABLED
  PRINTER_OBJ.begin(PRINTER_BAUD);
#else
  // Se debug esta ativo, ja iniciou com 115200.
  // Se a impressora exigir 9600, teremos conflito.
  // O usuario optou por desativar Debug, entao DEBUG_ENABLED deve ser 0.
  // Se por acaso ligar debug, a impressora vai receber lixo se baud for
  // diferente.
  PRINTER_OBJ.updateBaudRate(PRINTER_BAUD);
#endif
#else
  // Inicializa Serial 2 para a Impressora
  printerSerial.begin(PRINTER_BAUD, SERIAL_8N1, -1, PRINTER_TX_PIN);
#endif

  DBGLN(">>> PrinterManager: Iniciada");
}

void imprimirEtiqueta(Receita r, int contador, DataProducao data, int re) {
  if (r.id == 0)
    return;

  // --- DADOS PARA O ZPL ---
  int dia = data.dia;
  int mes = data.mes;
  int ano = data.ano;
  int reValor = re;

  unsigned long codigoProd = atol(r.codigo);

  // Buffer para o comando ZPL (Aumentado para suportar Logo)
  // [FIX] Usando static para evitar Stack Overflow (que causa tela
  // branca/reboot)
  static constexpr size_t ZPL_BUFFER_SIZE = 8192;
  static char zplBuffer[ZPL_BUFFER_SIZE];

  // Prepara a seção do Codigo de Barras (Condicional)
  char barcodeSection[256] = "";
  // Se tiver barcode preenchido (tamanho > 0), monta o ZPL.
  // Se nao tiver, barcodeSection continua vazio "" e nada é impresso nessa
  // parte.
  if (r.barcode[0] != '\0') {
    snprintf(barcodeSection, sizeof(barcodeSection),
             "^BY3,2,85^FT329,423^BEI,,Y,N\r\n^FD%s^FS\r\n", r.barcode);
  }

  // CODIGO ORIGINAL (RESTAURADO E ADAPTADO)
  int escrito = snprintf(
      zplBuffer, ZPL_BUFFER_SIZE,
      // HEADER SIMPLIFICADO
      "^XA\r\n"
      "^PW400\r\n"
      "^LL0599\r\n"
      // Removido ^CI28 (UTF-8) para compatibilidade maxima

      // LOGO MSA (Texto Simples)
      "^FO50,30^A0N,80,80^FDMSA^FS\r\n"

      "^FT375,547^A0I,32,31^FH\\^FDCODIGO: %06lu^FS\r\n"
      "^FT375,523^A0I,20,19^FH\\^FD%s^FS\r\n"
      "%s" // Barcode
      "^FO13,365^GB371,0,8^FS\r\n"
      "^FT356,330^A0I,31,31^FH\\^FDFAB:^FS\r\n"

      // Data - Removidos comandos de relogio (^SL, ^FC) que podem travar
      "^FT287,330^A0I,31,31\r\n"
      "^FD%02u/%02u/%02u^FS\r\n"

      "^FT341,287^A0I,31,31^FH\\^FDRE:^FS\r\n"
      "^FT287,287^A0I,31,31^FH\\^FD%04u^FS\r\n"
      "^FT372,233^A0I,31,31^FH\\^FDSEQUENCIAL:^FS\r\n"
      "^FT188,233^A0I,31,31^FH\\^FD%03lu^FS\r\n"
      "^PQ1,0,1,Y^XZ\r\n",
      (unsigned long)codigoProd, r.descricao,
      barcodeSection, // Passamos o bloco inteiro (ou vazio)
      (unsigned int)dia, (unsigned int)mes, (unsigned int)ano, reValor,
      (unsigned long)contador);

  if (escrito > 0 && escrito < (int)ZPL_BUFFER_SIZE) {
    // Envia para impressora
    PRINTER_OBJ.write(reinterpret_cast<const uint8_t *>(zplBuffer),
                      (size_t)escrito);
    PRINTER_OBJ.flush();
    DBGF(">>> ETIQUETA IMPRESSA: %s (Seq: %d) <<<\n", r.descricao, contador);
  } else {
    DBGLN("Erro ao montar ZPL!");
  }
}
