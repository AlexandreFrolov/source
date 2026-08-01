#include <Arduino_GFX_Library.h>

#define ROTATION 0

#define GFX_BL 46  // backlight

Arduino_DataBus *bus = new Arduino_ESP32SPI(41 /* DC */, 42 /* CS */, 40 /* SCK */, 45 /* MOSI */);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 39 /* RST */, 0 /* rotation */, false /* IPS */,
  172 /* width */, 320 /* height */,
  34 /*col_offset1*/, 0 /*row_offset1*/,
  34 /*col_offset2*/, 0 /*row_offset2*/);

void setup(void) {
  Serial.begin(115200);
  Serial.println("Arduino_GFX Hello World example");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  gfx->setRotation(ROTATION);
  gfx->fillScreen(RGB565_BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_RED);
  gfx->println("Hello World!");

  delay(5000);
}

void loop() {
  gfx->setCursor(random(gfx->width()), random(gfx->height()));
  gfx->setTextColor(random(0xffff), random(0xffff));
  gfx->setTextSize(random(6), random(6), random(2));
  gfx->println("Hello World!");

  delay(1000);
}