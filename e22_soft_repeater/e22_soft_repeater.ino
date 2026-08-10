/*
 * Полностью рабочий программный ретранслятор LoRa (ESP32-S3 + E22)
 * Точно эмулирует аппаратный репитер E22 (Пересылка из NETID 0x02 в NETID 0x10)
 */

#include "LoRa_E22.h"

// ---------- Пины E22 (ESP32-S3) ----------
#define LORA_RX_PIN  17   // ESP32 RX <- E22 TXD
#define LORA_TX_PIN  18   // ESP32 TX -> E22 RXD
#define LORA_AUX_PIN 16   // E22 AUX
#define LORA_M0_PIN  5    // E22 M0
#define LORA_M1_PIN  6    // E22 M1

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

// ---------- Сетевые параметры ----------
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
  Serial.println(F("  ESP32-S3 LoRa Software Repeater (Working)  "));
  Serial.println(F("=============================================="));

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  if (configureSoftRepeater()) {
    Serial.println(F("[SYSTEM] Ретранслятор готов! Прослушивание эфира (NETID 0x02)..."));
  } else {
    Serial.println(F("[SYSTEM ERROR] Ошибка инициализации E22!"));
  }
}

void loop() {
  if (e22.available() > 0) {
    // Читаем сообщение
    ResponseContainer rc = e22.receiveMessageRSSI();

    if (rc.status.code == 1) {
      String payload = rc.data;
      int rssi = (int)rc.rssi - 256;

      // Игнорируем эхо-ответы E22, если они попадут в буфер
      if (payload.length() == 3 && (uint8_t)payload[0] == 0xC1) {
        return;
      }

      Serial.println(F("\n----------------------------------------------"));
      Serial.printf("[RX REPEATER] Перехвачен пакет (%d байт) | RSSI: %d dBm\r\n", 
                    payload.length(), rssi);

      // Дамп данных
      Serial.print(F("[DATA HEX]: "));
      printHexBuffer((const uint8_t*)payload.c_str(), payload.length());
      
      Serial.print(F("[DATA ASCII]: "));
      Serial.println(payload);

      // --- ПЕРЕДАЧА ---
      // 1. Быстро переключаем NETID модуля на сеть Приёмника (0x10)
      setNetIDFast(DST_NETID);

      Serial.printf("[TX REPEATER] Переизлучение кадра в NETID 0x%02X...\r\n", DST_NETID);

      // 2. Отправляем исходные байты кадра как есть (прозрачный режим)
      ResponseStatus rs = e22.sendMessage(payload);

      if (rs.code == 1) {
        Serial.println(F("[TX SUCCESS] Пакет переизлучен!"));
      } else {
        Serial.print(F("[TX ERROR] Ошибка передачи: "));
        Serial.println(rs.getResponseDescription());
      }

      // 3. Возвращаем NETID на сеть Передатчика (0x02)
      setNetIDFast(SRC_NETID);

      Serial.println(F("----------------------------------------------"));
    }
  }
}

// Быстрая смена NETID напрямую без чтения конфигурации по UART
void setNetIDFast(uint8_t netId) {
  // Переводим модуль в режим настройки (M0=1, M1=1)
  digitalWrite(LORA_M0_PIN, HIGH);
  digitalWrite(LORA_M1_PIN, HIGH);
  delay(20);

  // Прямая запись регистра NETID (Адрес регистра NETID в E22 = 0x02)
  // Команда 0xC2 — запись регистров без сохранения в Flash
  uint8_t setNetIdCmd[] = {0xC2, 0x02, 0x01, netId};
  LoRaSerial.write(setNetIdCmd, sizeof(setNetIdCmd));
  LoRaSerial.flush();
  delay(20);

  // Очищаем служебный ответ модуля (C1 ...), чтобы он не попал в поток данных
  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }

  // Возвращаем режим NORMAL (M0=0, M1=0)
  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);
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

  // 1. Отключаем внутренний репитер
  cfg.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;

  // 2. Прозрачный режим (Transparent): ретранслятор пробрасывает кадр "как есть"
  cfg.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;

  // 3. Мощность 10 dBm
  cfg.OPTION.transmissionPower = POWER_10;

  // 4. Включаем RSSI
  cfg.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

  // 5. Адрес широковещательного приёма
  cfg.ADDH = 0xFF;
  cfg.ADDL = 0xFF;

  // 6. Исходный NETID передатчика и канал
  cfg.NETID = SRC_NETID;
  cfg.CHAN = LORA_CHANNEL;

  ResponseStatus rs = e22.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
  
  if (rs.code == 1) {
    Serial.println(F("[CFG OK] Настройка записана."));
  } else {
    return false;
  }
  
  e22.setMode(MODE_0_NORMAL);
  delay(100);

  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }

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