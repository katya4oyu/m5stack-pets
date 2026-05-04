#include <M5Unified.h>

int mood = 0;

void drawPet() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);

  const char* faces[] = {":)", ":D", ":|"};
  M5.Display.drawString(faces[mood], M5.Display.width() / 2, M5.Display.height() / 2);

  M5.Display.setTextSize(1);
  M5.Display.drawString("Button A changes mood", M5.Display.width() / 2, M5.Display.height() - 24);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  drawPet();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    mood = (mood + 1) % 3;
    drawPet();
  }

  delay(16);
}
