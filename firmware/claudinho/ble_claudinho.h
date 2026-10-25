// Transporte Bluetooth do Claudinho (BLE, NimBLE). No lugar do POST /evento e
// /estado por Wi-Fi, o PC escreve linhas de texto no servico abaixo:
//   E{json}\n  evento (o mesmo corpo do POST /evento)
//   S{json}\n  estado (o mesmo corpo do POST /estado)
// O json pode vir em varios pedacos (MTU pequeno): o fim e o \n. Quem escreve e
// a ponte do PC (host/ponte_ble.py), que traduz o curl dos hooks em BLE.
// O servico e o de "UART Nordic" (NUS); o nome anunciado e Claudinho-XXXX.
// Os pedidos entram numa fila no callback do Bluetooth e so sao tratados no
// loop() (bleCuida), no mesmo nucleo do resto do firmware.
#pragma once
#include <ArduinoJson.h>
#include <esp_mac.h>
#include <NimBLEDevice.h>

#define BLE_SERVICO "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

static const int BLE_FILA = 8, BLE_MSG = 640;
static char bleFila[BLE_FILA][BLE_MSG];
static volatile int bleLe = 0, bleEscreve = 0;
static char bleLinha[BLE_MSG]; static int bleLinhaN = 0;
static volatile bool bleConectado = false;
static String bleNomeStr;

inline const char* bleNome() { return bleNomeStr.c_str(); }

class BleServidorCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { bleConectado = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    bleConectado = false; NimBLEDevice::startAdvertising();
  }
};

class BleRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    NimBLEAttValue v = c->getValue();
    for (size_t i = 0; i < v.length(); i++) {
      char ch = (char)v.data()[i];
      if (ch == '\r') continue;
      if (ch == '\n') {
        if (bleLinhaN > 1) {
          int prox = (bleEscreve + 1) % BLE_FILA;
          if (prox != bleLe) {                       // fila cheia: descarta (evento perdido nao e grave)
            memcpy(bleFila[bleEscreve], bleLinha, bleLinhaN); bleFila[bleEscreve][bleLinhaN] = 0;
            bleEscreve = prox;
          }
        }
        bleLinhaN = 0;
      } else if (bleLinhaN < BLE_MSG - 1) bleLinha[bleLinhaN++] = ch;
      else bleLinhaN = 0;                            // linha grande demais: joga fora
    }
  }
};

inline void bleInicia() {
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_BT);
  char nome[24]; snprintf(nome, sizeof nome, "Claudinho-%02X%02X", mac[4], mac[5]);
  bleNomeStr = nome;
  NimBLEDevice::init(nome);
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new BleServidorCb());
  NimBLEService* svc = srv->createService(BLE_SERVICO);
  NimBLECharacteristic* rx = svc->createCharacteristic(BLE_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new BleRxCb());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICO);
  adv->setName(nome);
  adv->start();
  Serial.printf("ble: anunciando como %s\n", nome);
}

void aplicaEstado(JsonDocument& doc);
void aplicaEvento(JsonDocument& doc);
extern bool demo;

// No loop(): trata o que chegou pelo Bluetooth.
inline void bleCuida() {
  while (bleLe != bleEscreve) {
    char* m = bleFila[bleLe];
    JsonDocument doc;
    if (m[0] != 'E' && m[0] != 'S') Serial.printf("ble: tipo desconhecido '%c'\n", m[0]);
    else if (deserializeJson(doc, m + 1)) Serial.println("ble: json invalido");
    else if (!demo) { if (m[0] == 'E') aplicaEvento(doc); else aplicaEstado(doc); }
    bleLe = (bleLe + 1) % BLE_FILA;
  }
}
