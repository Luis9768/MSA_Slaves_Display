#include "NetworkSlave.h"
#include "Config.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

const char *SSID_MASTER = "MASTER_PRODUCAO";
QueueHandle_t networkQueue;

std::vector<Receita> listaReceitas;
volatile bool listaAtualizada = false;
Preferences preferences;

uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
String globalHostname = "MSA_SLAVE_UNKNOWN"; // Default
volatile bool requestNvsSync = false; // Flag to trigger NVS update in loop

unsigned long lastHeartbeatTime = 0;
long localVersion = 0;
// Smart Sync 2.0 variables
bool isSyncing = false;
std::vector<Receita> tempReceitas;

// Salva uma receita na NVS
void salvarReceitaNVS(Receita r) {
  if (r.id <= 0 || r.id > MAX_RECEITAS)
    return;
  char key[16];
  sprintf(key, "rec_%d", r.id);
  preferences.putBytes(key, &r, sizeof(Receita));
}

// Carrega todas as receitas da NVS
void carregarReceitasNVS() {
  listaReceitas.clear();
  for (int i = 1; i <= MAX_RECEITAS; i++) {
    char key[16];
    sprintf(key, "rec_%d", i);
    if (preferences.isKey(key)) {
      Receita r;
      preferences.getBytes(key, &r, sizeof(Receita));
      if (r.ativa) {
        listaReceitas.push_back(r);
      }
    }
  }
  listaAtualizada = true;
  localVersion = preferences.getLong("version", 0);
}

// Callback quando recebe dados (Rodando em ISR - Rápido!)
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(PacoteRede))
    return;

  PacoteRede pacote;
  memcpy(&pacote, incomingData, sizeof(pacote));

  // Envia para a fila para processar no Loop Principal
  xQueueSendFromISR(networkQueue, &pacote, NULL);
}

// Processamento real dos Pacotes (Rodando no Loop - Seguro)
void processPacket(PacoteRede pacote) {
  switch (pacote.tipo) {
  case PKG_DATA: { // 1 = Receita Individual
    Receita r = pacote.dados;

    if (isSyncing) {
      // FAST PATH: Just add to memory, no NVS yet.
      tempReceitas.push_back(r);
      // Optional: Update UI progress? Or just wait for end.
    } else {
      // HOT-FIX / SINGLE UPDATE: Save immediately
      salvarReceitaNVS(r);

      // Update List in place
      bool encontrado = false;
      for (auto &item : listaReceitas) {
        if (item.id == r.id) {
          if (item.ativa && !r.ativa) { // If it was active and now is inactive
            item.ativa = false;         // Mark inactive
          } else if (r.ativa) {         // If new recipe is active, update it
            item = r;
          }
          encontrado = true;
          break;
        }
      }
      if (!encontrado && r.ativa)
        listaReceitas.push_back(r);

      // Cleanup Inactives
      for (auto it = listaReceitas.begin(); it != listaReceitas.end();) {
        if (!it->ativa)
          it = listaReceitas.erase(it);
        else
          ++it;
      }

      listaAtualizada = true;
      // Version update for single packet (optional, keeps in sync)
      if (pacote.version > localVersion) {
        localVersion = pacote.version;
        preferences.putLong("version", localVersion);
      }
    }
    break;
  }

  case PKG_RESET: { // FORCE FACTORY RESET
    isSyncing = false;
    tempReceitas.clear();
    listaReceitas.clear();
    preferences.clear();
    preferences.putInt("reset_check_v2", 1); // Updated to v2
    preferences.putLong("version", 0);
    localVersion = 0;
    listaAtualizada = true;
    break;
  }

  case PKG_SYNC_START: {
    isSyncing = true;
    tempReceitas.clear();
    // NOTE: We do NOT clear listaReceitas yet to prevent flicker/empty screen
    // Wait for END to swap.
    Serial.println(">>> SYNC START (Smart mode)");
    break;
  }

  case PKG_SYNC_END: {
    if (isSyncing) {
      Serial.println(">>> SYNC END. Swap & Save.");
      // 1. Swap RAM Lists
      listaReceitas = tempReceitas;
      tempReceitas.clear();

      // 2. Set Version
      localVersion = pacote.version;

      // 3. Trigger NVS Dump (Background)
      requestNvsSync = true;

      listaAtualizada = true;
      isSyncing = false;
    }
    break;
  }

  case PKG_HEARTBEAT: {
    // Ack/Pong if needed
    break;
  }

  default:
    break;
  }
}

int32_t getWiFiChannel(const char *ssid) {
  if (int32_t n = WiFi.scanNetworks()) {
    for (uint8_t i = 0; i < n; i++) {
      if (!strcmp(ssid, WiFi.SSID(i).c_str()))
        return WiFi.channel(i);
    }
  }
  return 0;
}

void setupNetworkSlave() {
  // Inicia Queue
  networkQueue = xQueueCreate(50, sizeof(PacoteRede));

  // Inicia NVS
  preferences.begin("slave_db_v5", false);

  // Reset Forcado na primeira vez (Force Wipe requested by User)
  if (preferences.getInt("reset_check_v2", 0) == 0) {
    preferences.clear();
    preferences.putInt("reset_check_v2", 1);
    Serial.println(">>> MEMORY WIPED (Factory Reset v2) <<<");
  }

  carregarReceitasNVS();

  // --- INÍCIO CONFIGURAÇÃO OTA ---
  // Tenta conectar ao WiFi do Master para permitir OTA
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Disable Power Save to ensure 100% Receive Rx
  WiFi.begin(SSID_MASTER, OTA_WIFI_PASS);

  // Aguarda conexão por alguns segundos (não bloqueante eternamente)
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {

    // Configura Hostname com parte do MAC para ser único
    String hostname = OTA_HOSTNAME_PREFIX;
    hostname += "_";
    byte mac[6];
    WiFi.macAddress(mac);
    hostname += String(mac[5], HEX); // Usa ultimo byte do MAC
    hostname.toUpperCase();
    globalHostname = hostname;
    ArduinoOTA.setHostname(hostname.c_str());

    ArduinoOTA
        .onStart([]() {
          String type =
              (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        })
        .onEnd([]() {})
        .onProgress([](unsigned int progress, unsigned int total) {})
        .onError([](ota_error_t error) {});

    ArduinoOTA.begin();

    // Se conectou, o canal já está configurado pelo WiFi.begin
    // Mas para garantir o ESP-NOW, vamos verificar o canal
    int32_t channel = WiFi.channel();

  } else {
    // Fallback: Tenta encontrar o canal manual se não conectou
    int32_t channel = getWiFiChannel(SSID_MASTER);
    if (channel > 0) {
      esp_wifi_set_promiscuous(true);
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
      esp_wifi_set_promiscuous(false);
    } else {
    }
  }
  // --- FIM CONFIGURAÇÃO OTA ---

  if (esp_now_init() != ESP_OK) {
    return;
  }

  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Register Peer for Heartbeat
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddr, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
  }
}

void loopNetworkSlave() {
  ArduinoOTA.handle();

  if (millis() - lastHeartbeatTime > 5000) {
    lastHeartbeatTime = millis();
    PacoteRede pct;
    pct.tipo = PKG_HEARTBEAT; // Heartbeat
    pct.version = localVersion;
    strncpy(pct.dados.descricao, globalHostname.c_str(), 31); // Send Name
    esp_now_send(broadcastAddr, (uint8_t *)&pct, sizeof(pct));
  }

  // Processa Fila de Rede
  if (networkQueue != NULL) {
    PacoteRede pct;
    // Processa até 5 pacotes por vez para não travar a UI
    int count = 0;
    while (xQueueReceive(networkQueue, &pct, 0) == pdTRUE && count < 5) {
      processPacket(pct);
      count++;
    }
  }

  // Handle NVS Save Request (Outside of ISR/Callback to prevent WDT)
  if (requestNvsSync) {
    Serial.println(">>> PROCESSING NVS SYNC (Safe Mode)...");
    // Unregister CB to prevent incoming data conflict
    esp_now_unregister_recv_cb();

    // 1. Wipe Old
    for (int i = 1; i <= MAX_RECEITAS; i++) {
      char key[16];
      sprintf(key, "rec_%d", i);
      preferences.remove(key);
    }
    // 2. Save New
    for (const auto &r : listaReceitas) {
      salvarReceitaNVS(r);
    }
    preferences.putLong("version", localVersion);
    Serial.println(">>> NVS SYNC DONE.");

    requestNvsSync = false;
    // Re-register CB
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  }
}

std::vector<Receita> getListaReceitas() { return listaReceitas; }

bool novaListaDisponivel() { return listaAtualizada; }

void confirmarAtualizacaoLista() { listaAtualizada = false; }
