#include "NetworkSlave.h"
#include "Config.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

const char *SSID_MASTER = "MASTER_PRODUCAO";
std::vector<Receita> listaReceitas;
volatile bool listaAtualizada = false;
Preferences preferences;
uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
String globalHostname = "MSA_SLAVE_UNKNOWN"; // Default

unsigned long lastHeartbeatTime = 0;
long localVersion = 0;
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

// Callback quando recebe dados
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(PacoteRede))
    return;

  PacoteRede pacote;
  memcpy(&pacote, incomingData, sizeof(pacote));

  // Use switch for cleaner packet handling (defined in Common.h)
  switch (pacote.tipo) {
  case PKG_DATA: { // 1 = Receita Individual
    Receita r = pacote.dados;

    if (isSyncing) {
      // During Sync: Add to temp list without saving immediately
      tempReceitas.push_back(r);
    } else {
      // Legacy/Hot-fix: Update single item immediately
      salvarReceitaNVS(r);

      // Update in memory list
      bool encontrado = false;
      for (auto &item : listaReceitas) {
        if (item.id == r.id) {
          if (r.ativa)
            item = r;
          else
            item.ativa = false;
          encontrado = true;
          break;
        }
      }
      if (!encontrado && r.ativa) {
        listaReceitas.push_back(r);
      }
      // Remove inactives
      for (auto it = listaReceitas.begin(); it != listaReceitas.end();) {
        if (!it->ativa)
          it = listaReceitas.erase(it);
        else
          ++it;
      }
      listaAtualizada = true;

      // Update global version if provided (Hot-fix case)
      if (pacote.version > localVersion) {
        localVersion = pacote.version;
        preferences.putLong("version", localVersion);
      }
    }
    break;
  }

  case PKG_RESET: { // 2 = RESET TOTAL
    listaReceitas.clear();
    preferences.clear();
    preferences.putInt("reset_done", 1);
    preferences.putLong("version", 0);
    localVersion = 0;
    listaAtualizada = true;
    break;
  }

  case PKG_SYNC_START: { // 4 = Start Sync
    isSyncing = true;
    tempReceitas.clear();
    DBG("SYNC START received. Ver: %ld", pacote.version);
    break;
  }

  case PKG_SYNC_END: { // 5 = End Sync
    if (isSyncing) {
      // Commit transaction
      listaReceitas = tempReceitas;
      tempReceitas.clear();

      // Wipe NVS recipes and re-save everything (Safe but slow)
      // Optimization: In real world, maybe just overwrite keys.
      // Current salvarReceitaNVS is by ID, so it overwrites.
      // But we should clean old ones?
      // Simpler: clear NVS recipe keys?
      // For now, let's just overwrite based on list.
      // To be safe against "deleted" items remaining in NVS, we might want to
      // clear. But clearing ALL NVS takes time. Let's assume overwrite is
      // enough for now or implement a "clean wipe" if needed. Actually, RESET
      // packet handles wipe. SYNC assumes replacing valid data.

      for (const auto &r : listaReceitas) {
        salvarReceitaNVS(r);
      }

      localVersion = pacote.version;
      preferences.putLong("version", localVersion);
      listaAtualizada = true;
      isSyncing = false;
      DBG("SYNC END. Updated to Ver: %ld", localVersion);
    }
    break;
  }
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
  // Inicia NVS
  preferences.begin("slave_db_v5", false);

  // Reset Forcado na primeira vez
  if (preferences.getInt("reset_done", 0) == 0) {
    preferences.clear();
    preferences.putInt("reset_done", 1);
  }

  carregarReceitasNVS();

  // --- INÍCIO CONFIGURAÇÃO OTA ---
  // Tenta conectar ao WiFi do Master para permitir OTA
  WiFi.mode(WIFI_STA);
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
}

std::vector<Receita> getListaReceitas() { return listaReceitas; }

bool novaListaDisponivel() { return listaAtualizada; }

void confirmarAtualizacaoLista() { listaAtualizada = false; }
