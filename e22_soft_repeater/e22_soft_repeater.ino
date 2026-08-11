/* Программный (software) LoRa-репитер на ESP32-S3-N16R8 + Ebyte E22-900T22D
 *
 * В отличие от аппаратного режима репитера модуля (REG3.enableRepeater,
 * при котором ADDH/ADDL превращаются в пару "исходная сеть / целевая сеть"),
 * здесь ретрансляция делается программно самим микроконтроллером:
 *   1) модуль работает как обычный узел с фиксированной адресацией
 *      (fixedTransmission) и СВОИМ собственным адресом ADDH/ADDL;
 *   2) когда репитер принимает пакет, адресованный ему, он читает payload
 *      и RSSI, выводит их в Serial (Arduino IDE Monitor);
 *   3) затем репитер немедленно пересылает тот же payload дальше —
 *      отправляет его фиксированным сообщением на адрес конечного
 *      приёмника (см. e22_rx_via_repeater.ino) — и тоже логирует отправку.
 *
 * Поэтому все три узла (передатчик, репитер, приёмник) должны быть
 * НАСТРОЕНЫ В ОДНОЙ И ТОЙ ЖЕ СЕТИ NETID — здесь она больше не используется
 * модулем как "маршрут" (это было нужно только для аппаратного режима
 * репитера), а просто идентифицирует общую логическую сеть проекта.
 * Маршрутизация "кто кому пересылает" теперь целиком на стороне скетча
 * (константы REPEATER_ADDH/ADDL — "я", DEST_ADDH/ADDL — "куда пересылать").
 *
 * Плата: ESP32-S3-N16R8 (см. ESP32-S3-N16R8_User_Guide.pdf)
 * ВАЖНО: Tools -> USB CDC On Boot -> Disabled (иначе Serial.print() будет
 * молча пропадать — см. комментарий в e22_read_config.ino).
 *
 * Подключение (совпадает с e22_read_config.ino):
 *   E22-900T22D      ESP32-S3-N16R8
 *   VCC       ->     3V3 (отдельный LDO 3.3В + конденсатор 100-470 мкФ
 *                     у самого модуля рекомендуется из-за пикового тока TX)
 *   GND       ->     GND
 *   TXD       ->     GPIO17 (RX для ESP32)
 *   RXD       ->     GPIO18 (TX для ESP32)
 *   AUX       ->     GPIO16
 *   M0        ->     GPIO5
 *   M1        ->     GPIO6
 *
 * Библиотека: "LoRa_E22" (Renzo Mischianti, EByte_LoRa_E22)
 */

#include "LoRa_E22.h"

// ---------- Пины управления модулем (ESP32-S3-N16R8) ----------
#define PIN_AUX   16
#define PIN_M0    5
#define PIN_M1    6
#define PIN_RXD1  17   // ESP32 RX <- E22 TXD
#define PIN_TXD1  18   // ESP32 TX -> E22 RXD

LoRa_E22 e22(&Serial1, PIN_AUX, PIN_M0, PIN_M1, UART_BPS_RATE_9600);

// ---------- Собственный адрес репитера ("куда шлёт передатчик") ----------
#define REPEATER_ADDH  0x00
#define REPEATER_ADDL  0x02

// ---------- Адрес конечного приёмника ("куда репитер пересылает") ----------
#define DEST_ADDH      0x00
#define DEST_ADDL      0x03

// ---------- Общие для всей сети параметры радиоканала ----------
#define LORA_CHANNEL   19     // 850.125 + 19*1 = 869.125 МГц (поддиапазон ГКРЧ 868.7-869.2)
#define NET_ID         0x02   // одна и та же сеть у TX, репитера и RX

uint32_t rxCount = 0;
uint32_t fwdOkCount = 0;
uint32_t fwdFailCount = 0;

bool configureE22() {
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code != 1) {
    Serial.print(F("getConfiguration failed: "));
    Serial.println(c.status.getResponseDescription());
    c.close();
    return false;
  }
  Configuration configuration = *(Configuration *)c.data;

  configuration.ADDH = REPEATER_ADDH;
  configuration.ADDL = REPEATER_ADDL;
  configuration.NETID = NET_ID;
  configuration.CHAN = LORA_CHANNEL;

  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.airDataRate  = AIR_DATA_RATE_010_24;
  configuration.SPED.uartParity   = MODE_00_8N1;

  configuration.OPTION.transmissionPower = POWER_10;
  // Нужно для чтения RSSI входящих пакетов (receiveMessageRSSI)
  configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_ENABLED;

  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
  // LBT обязателен по требованиям ГКРЧ для поддиапазона 868.7-869.2 МГц,
  // распространяется как на исходный TX, так и на ретрансляцию репитером
  configuration.TRANSMISSION_MODE.enableLBT  = LBT_ENABLED;
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
  // Аппаратный режим репитера НЕ используем — репитер программный
  configuration.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;

  ResponseStatus rs = e22.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  c.close();
  return (rs.code == 1);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println(F("=== Программный LoRa-репитер (ESP32-S3-N16R8) ==="));

  Serial1.begin(9600, SERIAL_8N1, PIN_RXD1, PIN_TXD1);
  e22.begin();

  bool ok = configureE22();
  Serial.print(F("Конфигурация модуля: "));
  Serial.println(ok ? F("OK") : F("FAIL"));
  Serial.printf("Свой адрес: %02X:%02X, NETID: %02X, канал: %d\r\n",
                REPEATER_ADDH, REPEATER_ADDL, NET_ID, LORA_CHANNEL);
  Serial.printf("Пересылка на адрес: %02X:%02X\r\n", DEST_ADDH, DEST_ADDL);
  Serial.println(F("Ожидание пакетов..."));
  Serial.println(F("----------------------------------------"));
}

void loop() {
  if (e22.available() > 1) {
    ResponseContainer rc = e22.receiveMessageRSSI();

    if (rc.status.code != 1) {
      Serial.print(F("Ошибка приёма: "));
      Serial.println(rc.status.getResponseDescription());
      return;
    }

    rxCount++;
    String payload = rc.data;
    int rssi = (int)rc.rssi - 256;

    Serial.printf("[%lu] RX <- \"%s\" (RSSI %d dBm)\r\n",
                  (unsigned long)rxCount, payload.c_str(), rssi);

    // Ретрансляция: пересылаем тот же payload конечному приёмнику
    ResponseStatus rs = e22.sendFixedMessage(DEST_ADDH, DEST_ADDL, LORA_CHANNEL, payload);

    if (rs.code == 1) {
      fwdOkCount++;
    } else {
      fwdFailCount++;
    }

    Serial.printf("[%lu] TX -> %02X:%02X \"%s\" : %s (ok=%lu, fail=%lu)\r\n",
                  (unsigned long)rxCount, DEST_ADDH, DEST_ADDL, payload.c_str(),
                  rs.getResponseDescription().c_str(),
                  (unsigned long)fwdOkCount, (unsigned long)fwdFailCount);
    Serial.println(F("----------------------------------------"));
  }
}
