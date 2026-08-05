/* ESP32-S3-LCD-1.47B-M + Ebyte E22-900T22D
 * Передатчик текстовой строки на 868.9 МГц (канал 19 -> 869.125 МГц, поддиапазон ГКРЧ 868.7-869.2)
 * Пакеты идут через аппаратный репитер (см. e22_set_repeater_mode.ino):
 *   репитер сконфигурирован как ADDH=0x02 / ADDL=0x10, поэтому этот
 *   передатчик должен быть в "сети" NETID=0x02, чтобы репитер его слышал
 *   и пересылал в сеть NETID=0x10, где находится приёмник.
 *
 * Дисплей: ST7789 172x320, драйвер Arduino_GFX
 * Радио: Ebyte E22 в режиме фиксированной передачи (адресация ADDH/ADDL/CHAN),
 *        связь по UART через библиотеку EByte_LoRa_E22 (xreef/EByte_LoRa_E22_series_library)
 */

#include <Arduino_GFX_Library.h>
#include "LoRa_E22.h"

// ---------- Дисплей ST7789
#define TFT_DC   41
#define TFT_CS   42
#define TFT_SCK  40
#define TFT_MOSI 45
#define TFT_RST  39
#define TFT_BL   46

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED /* MISO не используется */);
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 0 /* rotation */, true /* IPS */,
                                      172 /* width */, 320 /* height */,
                                      34 /* col_offset1 */, 0 /* row_offset1 */,
                                      34 /* col_offset2 */, 0 /* row_offset2 */);

// ---------- Ebyte E22: UART + управляющие пины (свободные GPIO, не занятые LCD/SD/PSRAM) ----------
#define E22_UART      Serial1
#define E22_RXD_PIN   8   // ESP32 RX <- E22 TX
#define E22_TXD_PIN   9   // ESP32 TX -> E22 RX
#define E22_AUX_PIN   4
#define E22_M0_PIN    5
#define E22_M1_PIN    2

LoRa_E22 e22ttl(&E22_UART, E22_AUX_PIN, E22_M0_PIN, E22_M1_PIN, UART_BPS_RATE_9600);

// ---------- Параметры радиоканала ----------
#define MY_ADDH      0x00
#define MY_ADDL      0x01
#define DEST_ADDH    0x00   // адрес получателя (RX) — не меняется репитером
#define DEST_ADDL    0x02   // должен совпадать с MY_ADDH/MY_ADDL приёмника
#define LORA_CHANNEL 19      // 850.125 + 19*1 = 869.125 МГц

// NETID этого узла = "сеть-источник", которую слушает репитер (его REG0.ADDH)
#define MY_NETID     0x02

uint32_t txCount = 0;
char txbuf[64];

void drawStatus(const char *line1, const char *line2, const char *line3) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(4, 10);
  gfx->println(line1);
  gfx->setTextSize(1);
  gfx->setCursor(4, 50);
  gfx->println(line2);
  gfx->setCursor(4, 70);
  gfx->println(line3);
}

bool configureE22() {
  ResponseStructContainer c = e22ttl.getConfiguration();
  if (c.status.code != 1) {
    Serial.print("getConfiguration failed: ");
    Serial.println(c.status.getResponseDescription());
    return false;
  }
  Configuration configuration = *(Configuration *)c.data;

  configuration.ADDH = MY_ADDH;
  configuration.ADDL = MY_ADDL;
  configuration.NETID = MY_NETID;   // <-- ключевое добавление для маршрутизации через репитер
  configuration.CHAN = LORA_CHANNEL;

  configuration.SPED.uartBaudRate     = UART_BPS_9600;
  configuration.SPED.airDataRate      = AIR_DATA_RATE_010_24;
  configuration.SPED.uartParity       = MODE_00_8N1;

  //configuration.OPTION.transmissionPower = POWER_22; // максимум для 22dBm-версии модуля
  configuration.OPTION.transmissionPower = POWER_10;

  configuration.OPTION.RSSIAmbientNoise  = RSSI_AMBIENT_NOISE_ENABLED; // нужно для LBT

  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
  configuration.TRANSMISSION_MODE.enableLBT = LBT_ENABLED;
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

  ResponseStatus rs = e22ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  c.close();
  return (rs.code == 1);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  drawStatus("LoRa TX", "Init E22...", "");

  E22_UART.begin(9600, SERIAL_8N1, E22_RXD_PIN, E22_TXD_PIN);
  e22ttl.begin();

  bool ok = configureE22();
  drawStatus("LoRa TX", ok ? "E22 config OK" : "E22 config FAIL", "Ready to send...");
  delay(1000);
}

void loop() {
  static uint32_t lastSend = 0;

  if (millis() - lastSend < 5000) {
    return;
  }
  lastSend = millis();

  txCount++;
  snprintf(txbuf, sizeof(txbuf), "Hello LoRa #%lu", (unsigned long)txCount);
  String payload = String(txbuf);

  drawStatus("LoRa TX", ("TX: " + payload).c_str(), "Sending...");

  ResponseStatus rs = e22ttl.sendFixedMessage(DEST_ADDH, DEST_ADDL, LORA_CHANNEL, payload);

  drawStatus("LoRa TX", ("TX: " + payload).c_str(),
             rs.code == 1 ? "Send OK" : "Send FAIL");

  Serial.printf("Sent: \"%s\" status=%s\r\n", payload.c_str(), rs.getResponseDescription().c_str());
}
