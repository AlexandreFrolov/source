/*
 * Программный ретранслятор LoRa (ESP32-S3 + E22)
 * Умная сборка разрозненных посылок от TX (Окно 400мс)
 */

#include "LoRa_E22.h"

#define LORA_RX_PIN  17   // ESP32 RX <- E22 TXD
#define LORA_TX_PIN  18   // ESP32 TX -> E22 RXD
#define LORA_AUX_PIN 16   // E22 AUX
#define LORA_M0_PIN  5    // E22 M0
#define LORA_M1_PIN  6    // E22 M1

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

#define SRC_NETID    0x02   // Сеть передатчика (TX)
#define DST_NETID    0x10   // Сеть приёмника (RX)
#define LORA_CHANNEL 19     // Канал (869.125 МГц)

bool configureSoftRepeater();
void setNetIDFast(uint8_t netId);
void printHexBuffer(const uint8_t* buffer, uint16_t size);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n=============================================="));
  Serial.println(F(" ESP32-S3 LoRa Repeater (Smart Aggregator)   "));
  Serial.println(F("=============================================="));

  pinMode(LORA_AUX_PIN, INPUT);

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  if (configureSoftRepeater()) {
    Serial.println(F("[SYSTEM] Готов к приёму. Окно склейки: 400 мс."));
  } else {
    Serial.println(F("[SYSTEM ERROR] Ошибка E22!"));
  }
}

void loop() {
  if (e22.available() > 0) {
    String fullPayload = "";
    int lastRssi = 0;

    // Ждем окончания физического приёма первого куска
    while (digitalRead(LORA_AUX_PIN) == LOW) { delay(1); }

    // Читаем первый кусок
    ResponseContainer rc = e22.receiveMessageRSSI();
    if (rc.status.code == 1) {
      fullPayload = rc.data;
      lastRssi = (int)rc.rssi - 256;
    }

    // Игнорируем служебный мусор
    if (fullPayload.length() == 3 && (uint8_t)fullPayload[0] == 0xC1) {
      return;
    }

    // --- НАКОПИТЕЛЬНОЕ ОКНО ---
    // Слушаем эфир еще 400 мс на случай, если TX досылает остаток пакета
    unsigned long waitStart = millis();
    while (millis() - waitStart < 400) {
      
      if (e22.available() > 0) {
        while (digitalRead(LORA_AUX_PIN) == LOW) { delay(1); }
        
        ResponseContainer rc2 = e22.receiveMessageRSSI();
        if (rc2.status.code == 1) {
          String nextChunk = rc2.data;

          if (nextChunk.length() == 3 && (uint8_t)nextChunk[0] == 0xC1) continue;

          // Проверяем, не является ли следующий кусок обрезком предыдущего
          // Если первый кусок оканчивался на "LoR", а второй начинается на "a "
          fullPayload += nextChunk;
          
          // Сбрасываем таймер при получении нового куска
          waitStart = millis(); 
        }
      }
      delay(5);
    }

    // --- ОТПРАВКА СКЛЕЕННОГО СООБЩЕНИЯ ---
    if (fullPayload.length() > 0) {
      Serial.println(F("\n----------------------------------------------"));
      Serial.printf("[RX REPEATER] ПОЛНОСТЬЮ СОБРАН пакет (%d байт) | RSSI: %d dBm\r\n", 
                    fullPayload.length(), lastRssi);

      Serial.print(F("[DATA HEX]: "));
      printHexBuffer((const uint8_t*)fullPayload.c_str(), fullPayload.length());

      Serial.print(F("[DATA ASCII]: "));
      Serial.println(fullPayload);

      // 1. Переключаем сеть
      setNetIDFast(DST_NETID);

      Serial.printf("[TX REPEATER] Переизлучение в NETID 0x%02X...\r\n", DST_NETID);

      // 2. Отправляем единую строку
      e22.sendMessage(fullPayload);

      // 3. Ждем окончания передачи
      while (digitalRead(LORA_AUX_PIN) == LOW) { delay(1); }

      // 4. Возвращаем сеть
      setNetIDFast(SRC_NETID);

      Serial.println(F("[TX SUCCESS] Пакет передан целиком!"));
      Serial.println(F("----------------------------------------------"));
    }
  }
}

void setNetIDFast(uint8_t netId) {
  while (digitalRead(LORA_AUX_PIN) == LOW) { delay(1); }

  digitalWrite(LORA_M0_PIN, HIGH);
  digitalWrite(LORA_M1_PIN, HIGH);
  delay(10);

  uint8_t setNetIdCmd[] = {0xC2, 0x02, 0x01, netId};
  LoRaSerial.write(setNetIdCmd, sizeof(setNetIdCmd));
  LoRaSerial.flush();
  delay(10);

  while (LoRaSerial.available()) { LoRaSerial.read(); }

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);

  while (digitalRead(LORA_AUX_PIN) == LOW) { delay(1); }
}

bool configureSoftRepeater() {
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code != 1) { c.close(); return false; }

  Configuration cfg = *(Configuration*) c.data;
  c.close();

  cfg.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;
  cfg.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;
  cfg.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
  cfg.OPTION.transmissionPower = POWER_10;

  cfg.ADDH = 0xFF;
  cfg.ADDL = 0xFF;
  cfg.NETID = SRC_NETID;
  cfg.CHAN = LORA_CHANNEL;

  ResponseStatus rs = e22.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
  if (rs.code != 1) return false;
  
  e22.setMode(MODE_0_NORMAL);
  delay(100);

  while (LoRaSerial.available()) { LoRaSerial.read(); }
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