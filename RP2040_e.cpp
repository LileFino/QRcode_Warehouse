GitHub Copilot Chat Assistant

已按你的要求把按键改为 GPIO9..13，并按我之前建议的简化逻辑实现：
- A (GP9)：+1
- B (GP10)：-1
- C (GP11)：+10
- D (GP12)：-10
- E (GP13)：短按：切换到下一个分组（group）；长按（>=1500 ms）：进入睡眠（退出操作）

SPI 保持在 GPIO0..3（MOSI=GP0, MISO=GP1, SCK=GP2, CS=GP3），显示控制引脚 DC/RST/BUSY 保留在 GP20/GP21/GP22。若需改动请告诉我。

下面是完整可编译的 Arduino-for-Pico 源码（直接复制到 .cpp/.ino）：

```c++
/*
  RP2040 (Raspberry Pi Pico) 版本
  SPI: MOSI=GP0, MISO=GP1, SCK=GP2, CS=GP3
  Buttons: A=GP9, B=GP10, C=GP11, D=GP12, E=GP13
  DC=GP20, RST=GP21, BUSY=GP22
  依赖库：GxEPD2, Adafruit_GFX, qrcode
*/

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <qrcode.h>

// 显示驱动（按你的屏幕类型调整）
#define GxEPD2_DRIVER_CLASS GxEPD2_290_T5D

// SPI GPIO0..3
#define MOSI_PIN   0  // GP0
#define MISO_PIN   1  // GP1
#define SCK_PIN    2  // GP2
#define CS_PIN     3  // GP3

// 显示控制
#define DC_PIN    20  // GP20
#define RST_PIN   21  // GP21
#define BUSY_PIN  22  // GP22

// 五个按键
#define BUTTON_A_PIN  9   // GP9  (+1)
#define BUTTON_B_PIN  10  // GP10 (-1)
#define BUTTON_C_PIN  11  // GP11 (+10)
#define BUTTON_D_PIN  12  // GP12 (-10)
#define BUTTON_E_PIN  13  // GP13 (group select / exit)

// 参数
const unsigned long LONG_PRESS_TRIGGER_TIME = 1500;
const unsigned long INACTIVITY_TIMEOUT = 180000; // 3 分钟

const int LABEL_X = 10;
const int LABEL_Y = 90;
const int LABEL_W = 160;
const int LABEL_H = 32;

GxEPD2_BW<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(
  GxEPD2_DRIVER_CLASS(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

struct DisplayData { const char* label; int code; };
struct Group { const char* name; const DisplayData* items; int count; };

const DisplayData groupA[] = {
  {"A1-1", 11028}, {"A1-2", 11029}, {"A1-3", 11030},
  {"A1-4", 11031}, {"A1-5", 11032}
};
#define CLONE_GROUP(name) const DisplayData name[] = { \
  {"A1-1", 11028}, {"A1-2", 11029}, {"A1-3", 11030}, \
  {"A1-4", 11031}, {"A1-5", 11032} }
CLONE_GROUP(groupB);
CLONE_GROUP(groupC);
CLONE_GROUP(groupD);
CLONE_GROUP(groupE);
CLONE_GROUP(groupF);
CLONE_GROUP(groupG);
CLONE_GROUP(groupH);

const Group groups[] = {
  {"Group A", groupA, sizeof(groupA)/sizeof(DisplayData)},
  {"Group B", groupB, sizeof(groupB)/sizeof(DisplayData)},
  {"Group C", groupC, sizeof(groupC)/sizeof(DisplayData)},
  {"Group D", groupD, sizeof(groupD)/sizeof(DisplayData)},
  {"Group E", groupE, sizeof(groupE)/sizeof(DisplayData)},
  {"Group F", groupF, sizeof(groupF)/sizeof(DisplayData)},
  {"Group G", groupG, sizeof(groupG)/sizeof(DisplayData)},
  {"Group H", groupH, sizeof(groupH)/sizeof(DisplayData)}
};
const int groupCount = sizeof(groups) / sizeof(Group);

int currentGroupIndex = 0;
int currentItemIndex = 0;
unsigned long lastInteractionTime = 0;
bool pendingFullRefresh = false;

// 按键检测状态
unsigned long ePressStart = 0;
bool eHeld = false;

// 唤醒标志（中断）
volatile bool wakeFlag = false;
void wakeISR() { wakeFlag = true; }

void displayLabelOnly(const DisplayData& data) {
  display.setFont(&FreeMonoBold18pt7b);
  display.setPartialWindow(LABEL_X, LABEL_Y - LABEL_H, LABEL_W, LABEL_H);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(LABEL_X, LABEL_Y);
    display.print(data.label);
  } while (display.nextPage());
}

void displayQRCodeOnly(const DisplayData& data) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(1)];
  char qrText[32];
  snprintf(qrText, sizeof(qrText), "KUWEI-%d", data.code);
  qrcode_initText(&qrcode, qrcodeData, 1, ECC_LOW, qrText);

  const int qrScale = 5;
  const int qrSize = qrcode.size * qrScale;
  const int qrMargin = 10;
  const int qrX = display.width() - qrSize - qrMargin;
  const int qrY = (display.height() - qrSize) / 2;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(LABEL_X, LABEL_Y);
    display.print(data.label);
    for (uint8_t j = 0; j < qrcode.size; j++) {
      for (uint8_t i = 0; i < qrcode.size; i++) {
        if (qrcode_getModule(&qrcode, i, j)) {
          display.fillRect(qrX + i * qrScale, qrY + j * qrScale, qrScale, qrScale, GxEPD_BLACK);
        }
      }
    }
  } while (display.nextPage());

  display.powerOff();
}

void showSleepMessage() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(20, display.height() / 2);
    display.print("Entering Sleep...");
  } while (display.nextPage());
  display.powerOff();
}

void enterSleep() {
  showSleepMessage();
  delay(200);

  // 启用外部中断唤醒
  wakeFlag = false;
  attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), wakeISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), wakeISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_C_PIN), wakeISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_D_PIN), wakeISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_E_PIN), wakeISR, FALLING);

  // 等待按键唤醒（非真正低功耗 STOP）
  while (!wakeFlag) {
    delay(50);
  }

  detachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_C_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_D_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_E_PIN));

  displayQRCodeOnly(groups[currentGroupIndex].items[currentItemIndex]);
  lastInteractionTime = millis();
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // 初始化 SPI 指定引脚
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  display.init();
  display.setRotation(1);

  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);
  pinMode(BUTTON_D_PIN, INPUT_PULLUP);
  pinMode(BUTTON_E_PIN, INPUT_PULLUP);

  displayQRCodeOnly(groups[currentGroupIndex].items[currentItemIndex]);
  lastInteractionTime = millis();
}

void loop() {
  unsigned long now = millis();
  const Group& currentGroup = groups[currentGroupIndex];

  static bool aPrev = false, bPrev = false, cPrev = false, dPrev = false, ePrev = false;

  bool aNow = digitalRead(BUTTON_A_PIN) == LOW;
  bool bNow = digitalRead(BUTTON_B_PIN) == LOW;
  bool cNow = digitalRead(BUTTON_C_PIN) == LOW;
  bool dNow = digitalRead(BUTTON_D_PIN) == LOW;
  bool eNow = digitalRead(BUTTON_E_PIN) == LOW;

  // A: +1
  if (aNow && !aPrev) {
    currentItemIndex = (currentItemIndex + 1) % currentGroup.count;
    displayLabelOnly(currentGroup.items[currentItemIndex]);
    pendingFullRefresh = true;
    lastInteractionTime = now;
  }

  // B: -1
  if (bNow && !bPrev) {
    currentItemIndex = (currentItemIndex + currentGroup.count - 1) % currentGroup.count;
    displayLabelOnly(currentGroup.items[currentItemIndex]);
    pendingFullRefresh = true;
    lastInteractionTime = now;
  }

  // C: +10
  if (cNow && !cPrev) {
    currentItemIndex = (currentItemIndex + 10) % currentGroup.count;
    displayLabelOnly(currentGroup.items[currentItemIndex]);
    pendingFullRefresh = true;
    lastInteractionTime = now;
  }

  // D: -10
  if (dNow && !dPrev) {
    currentItemIndex = (currentItemIndex + currentGroup.count - (10 % currentGroup.count)) % currentGroup.count;
    displayLabelOnly(currentGroup.items[currentItemIndex]);
    pendingFullRefresh = true;
    lastInteractionTime = now;
  }

  // E: group select / exit (short press = next group, long press = sleep)
  if (eNow && !ePrev) {
    // 按下开始计时
    ePressStart = now;
    eHeld = true;
  }
  if (!eNow && ePrev && eHeld) {
    unsigned long dur = now - ePressStart;
    if (dur < LONG_PRESS_TRIGGER_TIME) {
      // 短按：下一个分组，重置项为 0
      currentGroupIndex = (currentGroupIndex + 1) % groupCount;
      currentItemIndex = 0;
      displayLabelOnly(groups[currentGroupIndex].items[currentItemIndex]);
      pendingFullRefresh = true;
      lastInteractionTime = now;
    } else {
      // 长按：进入睡眠（退出）
      enterSleep();
    }
    eHeld = false;
  }

  aPrev = aNow; bPrev = bNow; cPrev = cNow; dPrev = dNow; ePrev = eNow;

  // 2s 后全刷二维码
  if (pendingFullRefresh && (now - lastInteractionTime > 2000)) {
    displayQRCodeOnly(groups[currentGroupIndex].items[currentItemIndex]);
    pendingFullRefresh = false;
  }

  // 超时进入休眠
  if (now - lastInteractionTime > INACTIVITY_TIMEOUT) {
    enterSleep();
  }

  delay(10);
}