/*
  ESP32-S3 + EBYTE E22-900T22D
  ---------------------------------------------------------------
  ОДНОРАЗОВАЯ ПРОШИВКА КОНФИГУРАЦИИ:
  переводит модуль E22-900T22D в аппаратный режим репитера
  (store-and-forward на уровне модуля, без участия MCU в рабочем
  режиме).

  Как это работает:
    - В REG3 модуля выставляется бит "Enable repeater".
    - В режиме репитера ADDH/ADDL перестают быть адресом модуля и
      становятся парой NETID: "из какой сети принимать -> в какую
      сеть пересылать".
    - Конфигурация сохраняется в энергонезависимой памяти модуля
      (WRITE_CFG_PWR_DWN_SAVE), поэтому после однократного
      прошивания ESP32/MCU для работы репитера больше не нужен —
      модулю нужно только питание и M0=LOW, M1=LOW (Normal mode)
      на пинах управления.
*/

#include "LoRa_E22.h"

// ---------------- Пины E22 ----------------
#define LORA_RX_PIN  8    // ESP32 RX  <-- E22 TXD
#define LORA_TX_PIN  9    // ESP32 TX  --> E22 RXD
#define LORA_AUX_PIN 4    // E22 AUX
#define LORA_M0_PIN  5    // E22 M0
#define LORA_M1_PIN  2    // E22 M1

HardwareSerial LoRaSerial(1);
LoRa_E22 e22(&LoRaSerial, LORA_AUX_PIN, LORA_M0_PIN, LORA_M1_PIN, UART_BPS_RATE_9600);

// =================================================================
//  ПАРАМЕТРЫ РЕТРАНСЛЯЦИИ — ОБЯЗАТЕЛЬНО ОТРЕДАКТИРОВАТЬ ПОД СВОЮ СЕТЬ
// =================================================================
// В режиме репитера ADDH/ADDL — это НЕ адрес модуля, а таблица
// маршрутизации из одной записи: "сеть-источник -> сеть-приёмник".
// Пример: узлы сети A шлют данные с NETID=0x02, а получатели должны
// увидеть их как принадлежащие сети NETID=0x10. Тогда на репитере:
// Значения ниже согласованы с TX/RX скетчами: TX должен иметь
// configuration.NETID = 0x02, RX — configuration.NETID = 0x10.
#define REPEATER_ADDH   0x02   // NETID сети TX (сеть-источник)
#define REPEATER_ADDL   0x10   // NETID сети RX (сеть-приёмник)

// Канал должен совпадать у всех участников сети (TX, репитер, RX)
#define REPEATER_CHAN   19     // 850.125 + 19 = 869.125 МГц — как в TX/RX скетчах

void setup() {
  Serial.begin(115200);
  delay(1500);

  LoRaSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
  e22.begin();

  Serial.println(F("=== Чтение текущей конфигурации E22 ==="));
  ResponseStructContainer c = e22.getConfiguration();
  if (c.status.code != 1) {
    Serial.print(F("Ошибка чтения конфигурации: "));
    Serial.println(c.status.getResponseDescription());
    c.close();
    return;
  }

  Configuration configuration = *(Configuration*) c.data;
  c.close();

  Serial.println(F("Конфигурация прочитана, применяю режим репитера..."));

  configuration.ADDH = REPEATER_ADDH;
  configuration.ADDL = REPEATER_ADDL;
  configuration.CHAN = REPEATER_CHAN;

  // Собственный NETID репитера в этом режиме не используется, обнуляем
  configuration.NETID = 0x00;

  // Включаем аппаратный репитер (бит REG3.5)
  configuration.TRANSMISSION_MODE.enableRepeater = REPEATER_ENABLED;

  // Остальные поля (SPED, OPTION, LBT, RSSI и т.д.) оставляем как были —
  // они должны совпадать с настройками узлов сети.

  ResponseStatus rs = e22.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);

  Serial.print(F("Результат записи: "));
  Serial.println(rs.getResponseDescription());
  Serial.print(F("Код: "));
  Serial.println(rs.code);

  if (rs.code == 1) {
    Serial.println(F("Готово! Конфигурация репитера сохранена в модуле."));
    Serial.println(F("Теперь достаточно держать M0=LOW, M1=LOW и подавать питание —"));
    Serial.println(F("MCU для работы репитера больше не требуется."));
  } else {
    Serial.println(F("Запись не удалась — проверьте проводку AUX/M0/M1 и питание модуля."));
  }

  // ---- Проверочное чтение после записи ----
  delay(200);
  ResponseStructContainer c2 = e22.getConfiguration();
  if (c2.status.code == 1) {
    Configuration cfg2 = *(Configuration*) c2.data;
    Serial.println(F("=== Проверка после записи ==="));
    Serial.print(F("ADDH: 0x")); Serial.println(cfg2.ADDH, HEX);
    Serial.print(F("ADDL: 0x")); Serial.println(cfg2.ADDL, HEX);
    Serial.print(F("CHAN: ")); Serial.println(cfg2.CHAN, DEC);
    Serial.print(F("Repeater: ")); Serial.println(cfg2.TRANSMISSION_MODE.getRepeaterModeEnableByteDescription());
  }
  c2.close();
}

void loop() {
  // Пусто: конфигурация пишется один раз в setup().
  // Дальше плату/ESP32 можно отключить — репитеру для работы
  // достаточно питания и правильного состояния M0/M1.
}
