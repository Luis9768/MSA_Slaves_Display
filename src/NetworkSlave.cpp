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
unsigned long lastHeartbeatTime = 0;

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
}

// Callback quando recebe dados
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len != sizeof(PacoteRede))
    return;

  PacoteRede pacote;
  memcpy(&pacote, incomingData, sizeof(pacote));

  if (pacote.tipo == 1) { // Receita Individual
    Receita r = pacote.dados;

    // Salva na NVS imediatamente
    salvarReceitaNVS(r);

    // Verifica se já existe na lista
    bool encontrado = false;
    for (auto &item : listaReceitas) {
      if (item.id == r.id) {
        if (r.ativa) {
          item = r; // Atualiza
        } else {
          item.ativa = false;
        }
        encontrado = true;
        break;
      }
    }

    if (!encontrado && r.ativa) {
      listaReceitas.push_back(r);
    }

    // Limpeza de inativos
    for (auto it = listaReceitas.begin(); it != listaReceitas.end();) {
      if (!it->ativa) {
        it = listaReceitas.erase(it);
      } else {
        ++it;
      }
    }

    listaAtualizada = true;
  } else if (pacote.tipo == 2) { // RESET TOTAL (Vindo do Master)
    listaReceitas.clear();
    preferences.clear();                 // Limpa NVS
    preferences.putInt("reset_done", 1); // Mantém flag de reset inicial
    listaAtualizada = true;
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
    pct.tipo = 3; // Heartbeat
    // pct.dados = {0}; // Zerar dados opcional
    esp_now_send(broadcastAddr, (uint8_t *)&pct, sizeof(pct));
  }
}

std::vector<Receita> getListaReceitas() { return listaReceitas; }

bool novaListaDisponivel() { return listaAtualizada; }

void confirmarAtualizacaoLista() { listaAtualizada = false; }
