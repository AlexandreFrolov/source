/*
 * Программный ретранслятор LoRa (ESP32-S3 + E22-900T22D)
 * Работает в режиме широковещательного перехвата (0xFFFF)
 */

#include "LoRa_E22.h"

// ---------- Пины E22 (ESP32-S3-N16R8) ----------
#define LORA_RX_PIN  17   // ESP32 RX <- E22 TXD
#define LORA_TX_PIN  18   // ESP32 TX -> E22 RXD
#define LORA_AUX_PIN 16   // E22 AUX
#define LORA_M0_PIN  5    // E22 M0
#define LORA_M1_PIN  6    // E22 M1

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

// ---------- Сетевые параметры ----------
#define SRC_NETID    0x02   // Сеть TX
#define DST_NETID    0x10   // Сеть RX
#define LORA_CHANNEL 19     // 869.125 МГц

bool configureSoftRepeater();
void switchNetID(uint8_t newNetID);
void printHexBuffer(const uint8_t* buffer, uint16_t size);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n=============================================="));
  Serial.println(F("  ESP32-S3 LoRa Software Repeater (Fixed)    "));
  Serial.println(F("=============================================="));

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  if (configureSoftRepeater()) {
    Serial.println(F("[SYSTEM] Слушаю эфир (NETID: 0x02, Addr: 0xFFFF)..."));
  } else {
    Serial.println(F("[SYSTEM ERROR] Ошибка связи с E22! Проверьте пины M0, M1, AUX."));
  }
}

void loop() {
  if (e22.available() > 0) {
    // Чтение сообщения вместе с RSSI
    ResponseContainer rc = e22.receiveMessageRSSI();

    if (rc.status.code == 1) {
      String payload = rc.data;
      int rssi = (int)rc.rssi - 256;

      Serial.println(F("\n----------------------------------------------"));
      Serial.printf("[RX REPEATER] Пакет перехвачен! Байт: %d | RSSI: %d dBm\r\n", payload.length(), rssi);

      // Дамп данных
      Serial.print(F("[DATA HEX]: "));
      printHexBuffer((const uint8_t*)payload.c_str(), payload.length());
      Serial.print(F("[DATA ASCII]: "));
      Serial.println(payload);

      // --- ПЕРЕДАЧА ---
      // 1. Переключаемся на сеть Приёмника (NETID 0x10)
      switchNetID(DST_NETID);

      Serial.printf("[TX REPEATER] Пересылка в NETID 0x%02X...\r\n", DST_NETID);

      // 2. Отправляем сообщение обратно в эфир
      ResponseStatus rs = e22.sendMessage(payload);

      if (rs.code == 1) {
        Serial.println(F("[TX SUCCESS] Пакет переизлучен!"));
      } else {
        Serial.print(F("[TX ERROR] Ошибка передачи: "));
        Serial.println(rs.getResponseDescription());
      }

      // 3. Возвращаемся в сеть Передатчика (NETID 0x02)
      switchNetID(SRC_NETID);

      Serial.println(F("----------------------------------------------"));
    } else {
      Serial.print(F("[RX ERROR] Код ошибки приёма: "));
      Serial.println(rc.status.getResponseDescription());
    }
  }
}

// Быстрое переключение NETID
void switchNetID(uint8_t newNetID) {
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code == 1) {
    Configuration cfg = *(Configuration*) c.data;
    c.close();

    cfg.NETID = newNetID;
    // Пишем в RAM без износа Flash-памяти (WRITE_CFG_PWR_DWN_LOSE)
    e22.setConfiguration(cfg, WRITE_CFG_PWR_DWN_LOSE);
  } else {
    c.close();
  }
  e22.setMode(MODE_0_NORMAL);
  delay(20);
}

bool configureSoftRepeater() {
  Serial.println(F("[CFG] Настройка E22..."));
  
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code != 1) {
    c.close();
    return false;
  }

  Configuration cfg = *(Configuration*) c.data;
  c.close();

  // 1. Выключаем аппаратный репитер
  cfg.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;

  // 2. Включаем прозрачный режим для приёма/передачи
  cfg.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;

  // 3. Мощность 10 dBm (10 мВт)
  cfg.OPTION.transmissionPower = POWER_10;

  // 4. Включаем RSSI
  cfg.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

  // 5. Адрес 0xFFFF (Принимать абсолютно все адреса)
  cfg.ADDH = 0xFF;
  cfg.ADDL = 0xFF;

  // 6. Старт с NETID передатчика
  cfg.NETID = SRC_NETID;
  cfg.CHAN = LORA_CHANNEL;

  ResponseStatus rs = e22.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
  
  if (rs.code == 1) {
    Serial.println(F("[CFG OK] Конфигурация установлена."));
  } else {
    return false;
  }
  
  e22.setMode(MODE_0_NORMAL);
  delay(100);
  return true;
}

void printHexBuffer(const uint8_t* buffer, uint16_t size) {
  for (uint16_t i = 0; i < size; i++) {
    if (buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}