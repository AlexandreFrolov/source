/* ESP32-S3-LCD-1.47B-M + Ebyte E22-900T22D
 * Приёмник текстовой строки на 868.9 МГц (канал 19 -> 869.125 МГц, поддиапазон ГКРЧ 868.7-869.2)
 * Пакеты приходят через аппаратный репитер (см. e22_set_repeater_mode.ino):
 *   репитер сконфигурирован как ADDH=0x02 / ADDL=0x10, поэтому этот
 *   приёмник должен быть в "сети" NETID=0x10, куда репитер пересылает
 *   данные из сети NETID=0x02 (передатчик).
 *
 * Дисплей: ST7789 172x320, драйвер Arduino_GFX
 * Радио: Ebyte E22 в режиме фиксированной передачи + RSSI байт
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

// ---------- Параметры радиоканала (должны совпадать с параметрами передатчика) ----------
#define MY_ADDH      0x00
#define MY_ADDL      0x02  // = DEST_ADDH/DEST_ADDL в sender-скетче — репитер адрес не меняет
#define LORA_CHANNEL 19    // 850.125 + 19*1 = 869.125 МГц

// NETID этого узла = "сеть-приёмник", куда репитер пересылает пакеты (его REG0.ADDL)
#define MY_NETID     0x10

uint32_t rxCount = 0;

void drawReceived(const String &text, int rssiDbm, uint32_t count) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(4, 10);
  gfx->println("LoRa RX #" + String(count));

  gfx->setTextSize(1);
  gfx->setCursor(4, 45);
  gfx->println(text);

  gfx->setCursor(4, 300 - 20);
  gfx->println("RSSI: " + String(rssiDbm) + " dBm");
}

void drawWaiting() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(4, 10);
  gfx->println("LoRa RX");
  gfx->setTextSize(1);
  gfx->setCursor(4, 45);
  gfx->println("Waiting for packet...");
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

  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.airDataRate  = AIR_DATA_RATE_010_24;
  configuration.SPED.uartParity   = MODE_00_8N1;

//  configuration.OPTION.transmissionPower = POWER_22;
  configuration.OPTION.RSSIAmbientNoise  = RSSI_AMBIENT_NOISE_ENABLED;

  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
//  configuration.TRANSMISSION_MODE.enableLBT  = LBT_ENABLED;
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED; // добавляет RSSI-байт к принятому пакету

  ResponseStatus rs = e22ttl.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  c.close();
  return (rs.code == 1);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(4, 10);
  gfx->println("LoRa RX");
  gfx->setTextSize(1);
  gfx->setCursor(4, 45);
  gfx->println("Init E22...");

  E22_UART.begin(9600, SERIAL_8N1, E22_RXD_PIN, E22_TXD_PIN);
  e22ttl.begin();

  bool ok = configureE22();
  gfx->setCursor(4, 65);
  gfx->println(ok ? "E22 config OK" : "E22 config FAIL");
  delay(800);
  drawWaiting();
}

void loop() {
  if (e22ttl.available() > 1) {
    // RssiContainer уже разбирает payload + RSSI байт (dBm = raw - 256)
    ResponseContainer rc = e22ttl.receiveMessageRSSI();

    if (rc.status.code == 1) {
      rxCount++;
      String text = rc.data;
      int rssi = (int)rc.rssi - 256;

      drawReceived(text, rssi, rxCount);
      Serial.printf("RX #%lu: \"%s\" RSSI=%d dBm\r\n",
                    (unsigned long)rxCount, text.c_str(), rssi);
    } else {
      Serial.printf("RX error: %s\r\n", rc.status.getResponseDescription().c_str());
    }
  }
}
