/*
 * Стабильный программный ретранслятор с защитой от зацикливания и эха
 */

#include "LoRa_E22.h"

#define LORA_RX_PIN  17
#define LORA_TX_PIN  18
#define LORA_AUX_PIN 16
#define LORA_M0_PIN  5
#define LORA_M1_PIN  6

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

#define SRC_NETID    0x02
#define DST_NETID    0x10
#define LORA_CHANNEL 19

// Переменные для фильтрации дубликатов
String lastPayload = "";
unsigned long lastTxTime = 0;

bool configureSoftRepeater();
void setNetIDFast(uint8_t netId);
void printHexBuffer(const uint8_t* buffer, uint16_t size);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n=============================================="));
  Serial.println(F("  ESP32-S3 LoRa Repeater (Anti-Echo Fixed)   "));
  Serial.println(F("=============================================="));

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  if (configureSoftRepeater()) {
    Serial.println(F("[SYSTEM] Слушаю эфир... Защита от эха активна."));
  } else {
    Serial.println(F("[SYSTEM ERROR] Ошибка связи с E22!"));
  }
}

void loop() {
  if (e22.available() > 0) {
    ResponseContainer rc = e22.receiveMessageRSSI();

    if (rc.status.code == 1) {
      String payload = rc.data;
      int rssi = (int)rc.rssi - 256;

      // 1. Игнорируем служебный мусор от переключения режимов E22
      if (payload.length() == 3 && (uint8_t)payload[0] == 0xC1) {
        return;
      }

      // 2. ФИЛЬТР ДУБЛИКАТОВ: Игнорируем тот же самый пакет, если прошло меньше 2 секунд
      if (payload == lastPayload && (millis() - lastTxTime < 2000)) {
        Serial.println(F("[ANTI-ECHO] Заблокирован повторный пакет (эхо)"));
        return;
      }

      Serial.println(F("\n----------------------------------------------"));
      Serial.printf("[RX REPEATER] Принят пакет (%d байт) | RSSI: %d dBm\r\n", 
                    payload.length(), rssi);

      Serial.print(F("[DATA ASCII]: "));
      Serial.println(payload);

      // Запоминаем текущий пакет и время
      lastPayload = payload;
      lastTxTime = millis();

      // 3. ПЕРЕДАЧА
      setNetIDFast(DST_NETID);

      Serial.printf("[TX REPEATER] Переизлучение в NETID 0x%02X...\r\n", DST_NETID);
      ResponseStatus rs = e22.sendMessage(payload);

      if (rs.code == 1) {
        Serial.println(F("[TX SUCCESS] Пакет успешно передан!"));
      } else {
        Serial.print(F("[TX ERROR]: "));
        Serial.println(rs.getResponseDescription());
      }

      // 4. Очистка после передачи (пауза, чтобы дать пакету улететь в эфир)
      delay(300); 

      // Возвращаем NETID обратно для приема от TX
      setNetIDFast(SRC_NETID);

      // Очищаем буфер от остатков собственного перехваченного сигнала
      while (LoRaSerial.available()) {
        LoRaSerial.read();
      }

      Serial.println(F("----------------------------------------------"));
    }
  }
}

void setNetIDFast(uint8_t netId) {
  digitalWrite(LORA_M0_PIN, HIGH);
  digitalWrite(LORA_M1_PIN, HIGH);
  delay(15);

  uint8_t setNetIdCmd[] = {0xC2, 0x02, 0x01, netId};
  LoRaSerial.write(setNetIdCmd, sizeof(setNetIdCmd));
  LoRaSerial.flush();
  delay(15);

  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }

  digitalWrite(LORA_M0_PIN, LOW);
  digitalWrite(LORA_M1_PIN, LOW);
  delay(15);
}

bool configureSoftRepeater() {
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code != 1) {
    c.close();
    return false;
  }

  Configuration cfg = *(Configuration*) c.data;
  c.close();

  cfg.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;
  cfg.TRANSMISSION_MODE.fixedTransmission = FT_TRANSPARENT_TRANSMISSION;
  cfg.OPTION.transmissionPower = POWER_10;
  cfg.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;

  cfg.ADDH = 0xFF;
  cfg.ADDL = 0xFF;
  cfg.NETID = SRC_NETID;
  cfg.CHAN = LORA_CHANNEL;

  ResponseStatus rs = e22.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
  
  if (rs.code != 1) return false;
  
  e22.setMode(MODE_0_NORMAL);
  delay(100);

  while (LoRaSerial.available()) {
    LoRaSerial.read();
  }

  return true;
}