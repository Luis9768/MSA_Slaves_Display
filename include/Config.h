#ifndef CONFIG_H
#define CONFIG_H

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define MAX_RECEITAS 200

// Touch Pins (Confirmed by simple check: 33, 36, 32, 39, 25)
#define TOUCH_XPT_CS 33
#define TOUCH_XPT_IRQ 36
#define TOUCH_XPT_MOSI 32
#define TOUCH_XPT_MISO 39
#define TOUCH_XPT_CLK 25

// Printer Config (Placa Final: Usa Serial Padrao 1/3)
// ATENCAO: Debug via USB sera DESATIVADO pois a porta sera usada pela
// impressora
#define PRINTER_USES_MAIN_SERIAL 1
#define PRINTER_BAUD 115200

// Se a impressora usa a Serial principal, nao podemos ter debug
#if PRINTER_USES_MAIN_SERIAL
#define DEBUG_ENABLED 0
#else
#define DEBUG_ENABLED 1
#endif

// Sensor (Atualizado para 27)
#define PIN_SENSOR_PRODUTO 27

// LEDs RGB (Novos)
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define PIN_LED_ON                                                             \
  LOW // Assumindo anodo comum ou driver inversor (ajustar se necessario)
#define PIN_LED_OFF HIGH

// OTA Config
#define OTA_WIFI_PASS "YOUR_PASSWORD_HERE" // ALTERE PARA A SENHA DO MASTER
#define OTA_HOSTNAME_PREFIX "MSA_SLAVE"

// Macro de Log seguro
#if DEBUG_ENABLED
#define DBG(...)                                                               \
  Serial.printf(__VA_ARGS__);                                                  \
  Serial.println()
#define DBGF(...) Serial.printf(__VA_ARGS__)
#define DBGLN(...) Serial.println(__VA_ARGS__)
#else
#define DBG(...)
#define DBGF(...)
#define DBGLN(...)
#endif

#endif