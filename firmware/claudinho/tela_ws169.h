// Tela da Waveshare ESP32-S3-Touch-LCD-1.69 (ST7789V2 de 240x280 e toque
// CST816 por I2C). Mesma ideia da tela_e32.h: os comandos "Nextion" viram
// desenho direto, com as mesmas fontes e as mesmas coordenadas de 320x240.
// Como a tela e menor, tudo e desenhado num sprite de 320x240 na RAM e uma
// tarefa no outro nucleo copia o sprite para a tela, deitada (280x240),
// encolhido na horizontal (x 0,875) para ocupar a tela inteira. O toque volta
// para as coordenadas de 320x240. So entra na compilacao com PLACA_WS169.
//
// Pinos (os mesmos do port do claude-desktop-buddy para esta placa):
//   LCD SCK 6, MOSI 7, DC 4, CS 5, RST 8, luz 15; I2C SDA 11, SCL 10;
//   toque RST 13, INT 14; BOOT 0.
// A primeira revisao da placa (PLACA_WS169_V1) liga a trava de energia no
// GPIO35 (a V2.1, no 41). Esses pinos tambem sao da PSRAM octal, por isso a
// PSRAM fica desligada nesta compilacao.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#include "fontes_e32.h"

#if defined(PLACA_WS169_V1)
static const int WS_TRAVA = 35;   // segurar em nivel alto: continua ligada na bateria
static const int WS_PWR = 36;     // botao PWR (nivel baixo = apertado); divide o pino com a PSRAM octal
#else
static const int WS_TRAVA = 41;
static const int WS_PWR = 40;
#endif

class PainelWs : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 painel; lgfx::Bus_SPI bus; lgfx::Light_PWM luz;
public:
  PainelWs() {
    { auto c = bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0; c.freq_write = 80000000;
      c.pin_sclk = 6; c.pin_mosi = 7; c.pin_miso = -1; c.pin_dc = 4;
      bus.config(c); painel.setBus(&bus); }
    { auto c = painel.config();
      c.pin_cs = 5; c.pin_rst = 8; c.pin_busy = -1;
      c.memory_width = 240; c.memory_height = 320;          // o ST7789 tem 320 linhas; o vidro mostra 280
      c.panel_width = 240; c.panel_height = 280; c.offset_y = 20;
      c.invert = true; c.rgb_order = true; c.readable = false; c.bus_shared = false;   // rgb_order: vidro em BGR, sem isso o laranja sai azul
      painel.config(c); }
    { auto c = luz.config(); c.pin_bl = 15; c.invert = false; c.freq = 5000; c.pwm_channel = 0;
      luz.config(c); painel.setLight(&luz); }
    setPanel(&painel);
  }
};
static PainelWs wsPainel;

// O resto do firmware chama e32.<desenho>; aqui e32 e o sprite de 320x240,
// com luz, sono e toque repassados para a tela de verdade.
class TelaWs : public lgfx::LGFX_Sprite {
public:
  TelaWs() : lgfx::LGFX_Sprite(&wsPainel) {}
  void setBrightness(uint8_t v) { wsPainel.setBrightness(v); }
  void sleep() { wsPainel.sleep(); }
  void wakeup() { wsPainel.wakeup(); }
};
static TelaWs e32;
static int e32Fonte = -1;
static bool wsToqueOk = false;
static volatile bool wsSujo = true;   // o sprite mudou desde a ultima copia

static const float WS_ESCALA_X = 280.0f / 320.0f;

// Copia o sprite para a tela quando ele muda (no maximo ~30 vezes por
// segundo) e, por garantia, a cada meio segundo: alguns desenhos (os aneis)
// chamam e32 direto, sem passar por e32Cmd.
static void wsTarefaTela(void*) {
  uint32_t ult = 0;
  for (;;) {
    uint32_t agora = millis();
    if (wsSujo || agora - ult > 500) {
      wsSujo = false; ult = agora;
      wsPainel.startWrite();
      e32.pushRotateZoomWithAA(&wsPainel, 140, 120, 0, WS_ESCALA_X, 1.0f);
      wsPainel.endWrite();
    }
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

inline void e32Inicia() {
  Serial.setTxTimeoutMs(0);                // sem monitor aberto, o USB-CDC travaria cada print
  pinMode(WS_TRAVA, OUTPUT); digitalWrite(WS_TRAVA, HIGH);
  wsPainel.init();
  wsPainel.setRotation(1);                 // deitada, 280 x 240
  wsPainel.setBrightness(255);
  wsPainel.fillScreen(0);
  pinMode(WS_PWR, INPUT_PULLUP);
  // Toque CST816 (0x15) lido direto pelo Wire, como no port do claude-desktop-buddy.
  Wire.begin(11, 10); Wire.setClock(400000);
  pinMode(13, OUTPUT); digitalWrite(13, LOW); delay(10); digitalWrite(13, HIGH); delay(50);
  Wire.beginTransmission(0x15); wsToqueOk = Wire.endTransmission() == 0;
  if (wsToqueOk) { Wire.beginTransmission(0x15); Wire.write(0xFE); Wire.write(0x01); Wire.endTransmission(); }   // nao dormir sozinho
  Serial.printf("toque: cst816 %s\n", wsToqueOk ? "achado" : "NAO achado");
  e32.setPsram(false);
  e32.setColorDepth(16);
  if (!e32.createSprite(320, 240)) {       // 150 KB; se faltar RAM, 8 bits (75 KB, cores aproximadas)
    Serial.println("tela: sem RAM para 16 bits, usando 8");
    e32.setColorDepth(8);
    e32.createSprite(320, 240);
  }
  Serial.printf("tela: sprite %dx%d %d bits, RAM livre %u, maior bloco %u\n", e32.width(), e32.height(),
                e32.getColorDepth(), (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  e32.setAttribute(lgfx::utf8_switch, 0);  // os textos sao ISO-8859-1: cada byte e um caractere
  e32.fillScreen(0);
  xTaskCreatePinnedToCore(wsTarefaTela, "tela", 4096, nullptr, 1, nullptr, 0);
}

inline void e32UsaFonte(int f) {
  if (f < 0 || f > 9 || !FONTES_VLW[f]) f = 0;
  if (f == e32Fonte) return;
  e32.loadFont(FONTES_VLW[f]); e32Fonte = f;
}

// xstr x,y,w,h,fonte,cor,fundo,alinhaX,alinhaY,fundoCheio,"texto"
inline void e32Xstr(const char* c) {
  int x, y, w, h, f, ax, ay, cheio; unsigned cor, fundo;
  if (sscanf(c, "xstr %d,%d,%d,%d,%d,%u,%u,%d,%d,%d", &x, &y, &w, &h, &f, &cor, &fundo, &ax, &ay, &cheio) != 10) return;
  const char* a = strchr(c, '"'); const char* b = strrchr(c, '"');
  if (!a || b <= a) return;
  char t[128]; size_t n = min((size_t)(b - a - 1), sizeof t - 1); memcpy(t, a + 1, n); t[n] = 0;
  if (cheio == 1) e32.fillRect(x, y, w, h, (uint16_t)fundo);
  e32UsaFonte(f);
  e32.setTextColor((uint16_t)cor, (uint16_t)fundo);
  e32.setTextDatum(ax == 0 ? (ay == 1 ? middle_left : top_left) : ax == 2 ? (ay == 1 ? middle_right : top_right)
                                                                           : (ay == 1 ? middle_center : top_center));
  int tx = ax == 0 ? x : ax == 2 ? x + w : x + w / 2, ty = ay == 1 ? y + h / 2 : y;
  e32.setClipRect(x, y, w, h);
  e32.drawString(t, tx, ty);
  e32.clearClipRect();
}

// Os comandos do Nextion que o firmware usa; o resto e ignorado.
inline void e32Cmd(const char* c) {
  int a, b, d, e; unsigned cor;
  if (!strncmp(c, "fill ", 5)) { if (sscanf(c + 5, "%d,%d,%d,%d,%u", &a, &b, &d, &e, &cor) == 5) e32.fillRect(a, b, d, e, (uint16_t)cor); }
  else if (!strncmp(c, "xstr ", 5)) e32Xstr(c);
  else if (!strncmp(c, "line ", 5)) { if (sscanf(c + 5, "%d,%d,%d,%d,%u", &a, &b, &d, &e, &cor) == 5) e32.drawLine(a, b, d, e, (uint16_t)cor); }
  else if (!strncmp(c, "cirs ", 5)) { if (sscanf(c + 5, "%d,%d,%d,%u", &a, &b, &d, &cor) == 4) e32.fillCircle(a, b, d, (uint16_t)cor); }
  else if (!strncmp(c, "cir ", 4))  { if (sscanf(c + 4, "%d,%d,%d,%u", &a, &b, &d, &cor) == 4) e32.drawCircle(a, b, d, (uint16_t)cor); }
  else if (!strncmp(c, "dim=", 4))  { int v = constrain(atoi(c + 4), 0, 100); e32.setBrightness(v * 255 / 100); return; }
  else return;
  wsSujo = true;
}

// Toque: devolve true quando o estado muda (apertou = true / soltou = false),
// com a posicao em x, y ja nas coordenadas de 320x240. Le a cada 15 ms, no maximo.
// O botao PWR conta como um toque no centro da tela (so pressionar e soltar).
inline bool e32Toque(bool& apertou, int& x, int& y) {
  static bool tocando = false; static int ux = 160, uy = 120; static unsigned long ult = 0;
  if (millis() - ult < 15) return false;
  ult = millis();
  bool agora = false;
  if (wsToqueOk) {
    uint8_t d[5] = {0};
    Wire.beginTransmission(0x15); Wire.write(0x02);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x15, 5) == 5) {
      for (int i = 0; i < 5; i++) d[i] = Wire.read();
      int tx = ((d[1] & 0x0F) << 8) | d[2], ty = ((d[3] & 0x0F) << 8) | d[4];   // 0..239 x 0..279, retrato
      if ((d[0] & 0x0F) > 0 && (d[0] & 0x0F) <= 2 && tx < 240 && ty < 280) {      // fora da tela = toque fantasma
        agora = true;
        ux = constrain((int)(ty / WS_ESCALA_X), 0, 319); uy = constrain(239 - tx, 0, 239);   // tela deitada
      }
    }
  }
  if (!agora && digitalRead(WS_PWR) == LOW) { agora = true; ux = 160; uy = 120; }
  if (agora == tocando) return false;
  tocando = agora; apertou = agora; x = ux; y = uy;
  return true;
}
