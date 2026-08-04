/*
  ESP32-S3-LCD-1.47B-M  +  EBYTE E22-900T22D
  ---------------------------------------------------------------
  Чтение текущих параметров модуля LoRa (getConfiguration) и вывод
  их на встроенный дисплей ST7789 (172x320).

  Библиотеки (Arduino IDE -> Library Manager):
    - "LoRa_E22" / EByte_LoRa_E22_Series_Library (автор xreef / renzo mischianti)
    - "GFX Library for Arduino" (moononournation / Arduino_GFX_Library)
*/

#include <Arduino_GFX_Library.h>
#include "LoRa_E22.h"

// ---------------- Дисплей ST7789 172x320 ----------------
// Рабочая распиновка, подтверждённая на плате Waveshare ESP32-S3-LCD-1.47:
#define TFT_DC   41
#define TFT_CS   42
#define TFT_SCK  40
#define TFT_MOSI 45
#define TFT_RST  39
#define TFT_BL   46 

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* rotation */, true /* IPS */,
                                      172, 320,
                                      34 /* col_offset1 */, 0 /* row_offset1 */,
                                      34 /* col_offset2 */, 0 /* row_offset2 */);

// ---------------- LoRa E22-900T22D (UART) ----------------
#define LORA_RX_PIN  8    // ESP32 RX  <-- E22 TXD
#define LORA_TX_PIN  9    // ESP32 TX  --> E22 RXD
#define LORA_AUX_PIN 4    // E22 AUX
#define LORA_M0_PIN  5    // E22 M0
#define LORA_M1_PIN  2    // E22 M1

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

// ---------------- Вывод текста построчно ----------------
static int cursorY;

// Ширина области для текста в пикселях (экран 172 px минус отступы слева/справа)
#define TEXT_AREA_WIDTH 164

void printLine(const String &txt, uint16_t color = RGB565_WHITE) {
  gfx->setCursor(4, cursorY);
  gfx->setTextColor(color);
  gfx->print(txt);

  // Считаем реальную ширину строки в пикселях и сколько "физических" строк она займёт
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);

  int linesUsed = 1;
  if (w > TEXT_AREA_WIDTH) {
    linesUsed = (w + TEXT_AREA_WIDTH - 1) / TEXT_AREA_WIDTH; // округление вверх
  }

  cursorY += 16 * linesUsed;
}

void showConfiguration(Configuration cfg) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(1);
  cursorY = 6;

  printLine("E22-900T22D: configuration", RGB565_YELLOW);
  cursorY += 4;

  uint16_t addr = ((uint16_t)cfg.ADDH << 8) | cfg.ADDL;
  printLine("Addr : " + String(addr));
  printLine("NetID: " + String(cfg.NETID, DEC));
  printLine("Chan : " + String(cfg.CHAN, DEC) + " (" + cfg.getChannelDescription() + ")");

  // Поля ниже относятся к вложенным битовым структурам SPED / OPTION /
  // TRANSMISSION_POWER — сверьте точные названия методов в LoRa_E22.h
  printLine("UART : " + cfg.SPED.getUARTBaudRateDescription());
  printLine("Air  : " + cfg.SPED.getAirDataRateDescription());
  printLine("Parity: " + cfg.SPED.getUARTParityDescription());
  printLine("Power: " + cfg.OPTION.getTransmissionPowerDescription());
  printLine("SubPkt: " + cfg.OPTION.getSubPacketSetting());
  printLine("Fixed: " + cfg.TRANSMISSION_MODE.getFixedTransmissionDescription());
  printLine(" ");
  printLine("LBT  : " + cfg.TRANSMISSION_MODE.getLBTEnableByteDescription());
  printLine("RSSI : " + cfg.TRANSMISSION_MODE.getRSSIEnableByteDescription());
}

void printParametersSerial(struct Configuration cfg) {
  Serial.println(F("----------------------------------------"));
  Serial.print(F("AddH : ")); Serial.println(cfg.ADDH, HEX);
  Serial.print(F("AddL : ")); Serial.println(cfg.ADDL, HEX);
  Serial.print(F("NetID: ")); Serial.println(cfg.NETID, HEX);
  Serial.print(F("Chan : ")); Serial.print(cfg.CHAN, DEC);
  Serial.print(F(" -> ")); Serial.println(cfg.getChannelDescription());

  Serial.print(F("Parity   : ")); Serial.println(cfg.SPED.getUARTParityDescription());
  Serial.print(F("UART baud: ")); Serial.println(cfg.SPED.getUARTBaudRateDescription());
  Serial.print(F("Air rate : ")); Serial.println(cfg.SPED.getAirDataRateDescription());

  Serial.print(F("SubPacket: ")); Serial.println(cfg.OPTION.getSubPacketSetting());
  Serial.print(F("TX Power : ")); Serial.println(cfg.OPTION.getTransmissionPowerDescription());
  Serial.print(F("RSSI Amb : ")); Serial.println(cfg.OPTION.getRSSIAmbientNoiseEnable());

  Serial.print(F("WOR period: ")); Serial.println(cfg.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
  Serial.print(F("WOR TRX   : ")); Serial.println(cfg.TRANSMISSION_MODE.getWORTransceiverControlDescription());
  Serial.print(F("LBT       : ")); Serial.println(cfg.TRANSMISSION_MODE.getLBTEnableByteDescription());
  Serial.print(F("RSSI byte : ")); Serial.println(cfg.TRANSMISSION_MODE.getRSSIEnableByteDescription());
  Serial.print(F("Repeater  : ")); Serial.println(cfg.TRANSMISSION_MODE.getRepeaterModeEnableByteDescription());
  Serial.print(F("Fixed TX  : ")); Serial.println(cfg.TRANSMISSION_MODE.getFixedTransmissionDescription());
  Serial.println(F("----------------------------------------"));
}

void showError(const String &msg) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(4, 6);
  gfx->setTextColor(RGB565_RED);
  gfx->print("LoRa error:");
  gfx->setCursor(4, 22);
  gfx->print(msg);
}

void setup() {
  Serial.begin(115200);

  // ---- дисплей ----
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(4, 6);
  gfx->print("Init E22...");

  // ---- LoRa ----
  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  ResponseStructContainer c = e22.getConfiguration();

  if (c.status.code == 1 /* E22_SUCCESS */) {
    Configuration cfg = *(Configuration*) c.data;
    Serial.println(c.status.getResponseDescription());
    printParametersSerial(cfg);    // полный дамп в Serial для проверки
    showConfiguration(cfg);
  } else {
    Serial.println(c.status.getResponseDescription());
    showError(c.status.getResponseDescription());
  }

  c.close();
}

void loop() {
  // Основной обмен данными (передача/приём пакетов) добавляется здесь.
  // Не забудьте, что для приёма/передачи M0/M1 должны быть в режиме
  // "нормальная передача" (обычно LOW/LOW) — библиотека управляет ими
  // сама при вызове соответствующих методов sendMessage/receiveMessage.
}
