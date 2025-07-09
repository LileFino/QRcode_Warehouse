

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <qrcode.h>

// 选择所用墨水屏驱动类（此处为2.9英寸GDEH029A1）
#define GxEPD2_DRIVER_CLASS GxEPD2_290_T5D

// STM32 SPI引脚定义
#define CS_PIN   PA4
#define DC_PIN   PA3
#define RST_PIN  PA2
#define BUSY_PIN PA1

// 按键引脚（上拉输入，低电平触发）
#define BUTTON_A_PIN PB0
#define BUTTON_B_PIN PB1
#define BUTTON_C_PIN PB10

// -------------------- 可调节参数区域 ----------------------

// 长按启动时间（毫秒）
const unsigned long LONG_PRESS_TRIGGER_TIME = 1500;
// 长按后的连续切换间隔（毫秒）
const unsigned long LONG_PRESS_REPEAT_TIME = 500;

// C键切组的长按时间与重复时间
const unsigned long GROUP_SWITCH_TRIGGER_TIME = 1000;
const unsigned long GROUP_SWITCH_REPEAT_TIME = 500;

// 长按时每次跳过几个页面
const int LONG_PRESS_STEP = 3;

// 无操作进入休眠的时间（单位：毫秒）
// 默认3分钟 = 180000ms
const unsigned long INACTIVITY_TIMEOUT = 180000;

// 标签文字显示区域（适配最长标签，如 A4215-15）
const int LABEL_X = 10;       // 左边距
const int LABEL_Y = 90;       // 基线位置（字体底部线）
const int LABEL_W = 160;      // 宽度
const int LABEL_H = 32;       // 高度

// --------------------------------------------------------

GxEPD2_BW<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(
  GxEPD2_DRIVER_CLASS(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

struct DisplayData {
  const char* label;
  int code;
};

struct Group {
  const char* name;
  const DisplayData* items;
  int count;
};

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
  {"Group A", groupA, sizeof(groupA) / sizeof(DisplayData)},
  {"Group B", groupB, sizeof(groupB) / sizeof(DisplayData)},
  {"Group C", groupC, sizeof(groupC) / sizeof(DisplayData)},
  {"Group D", groupD, sizeof(groupD) / sizeof(DisplayData)},
  {"Group E", groupE, sizeof(groupE) / sizeof(DisplayData)},
  {"Group F", groupF, sizeof(groupF) / sizeof(DisplayData)},
  {"Group G", groupG, sizeof(groupG) / sizeof(DisplayData)},
  {"Group H", groupH, sizeof(groupH) / sizeof(DisplayData)}
};

const int groupCount = sizeof(groups) / sizeof(Group);

int currentGroupIndex = 0;
int currentItemIndex = 0;
unsigned long lastInteractionTime = 0;
bool pendingFullRefresh = false;

bool aButtonHeld = false, bButtonHeld = false, cButtonHeld = false;
unsigned long aPressStartTime = 0, bPressStartTime = 0, cPressStartTime = 0;
unsigned long aLastRepeatTime = 0, bLastRepeatTime = 0, cLastRepeatTime = 0;

// 仅刷新文字区域
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

// 全刷二维码 + 文字（文字位置保持一致）
void displayQRCodeOnly(const DisplayData& data) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(1)];
  char qrText[20];
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

    // 显示标签文字
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(LABEL_X, LABEL_Y);
    display.print(data.label);

    // 显示二维码
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

// 进入睡眠前提示
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

// 进入休眠模式
void enterSleep() {
  showSleepMessage();
  delay(1000);

  attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), [] {}, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), [] {}, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_C_PIN), [] {}, FALLING);

  HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
  SystemClock_Config();

  detachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN));
  detachInterrupt(digitalPinToInterrupt(BUTTON_C_PIN));

  displayQRCodeOnly(groups[currentGroupIndex].items[currentItemIndex]);
  lastInteractionTime = millis();
}

// 初始化
void setup() {
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  display.init();
  display.setRotation(1);

  pinMode(BUTTON_A_PIN, INPUT_PULLUP);
  pinMode(BUTTON_B_PIN, INPUT_PULLUP);
  pinMode(BUTTON_C_PIN, INPUT_PULLUP);

  displayQRCodeOnly(groups[currentGroupIndex].items[currentItemIndex]);
  lastInteractionTime = millis();
}

// 主循环
void loop() {
  unsigned long now = millis();
  const Group& currentGroup = groups[currentGroupIndex];

  static bool aPrev = false, bPrev = false, cPrev = false;

  bool aNow = digitalRead(BUTTON_A_PIN) == LOW;
  bool bNow = digitalRead(BUTTON_B_PIN) == LOW;
  bool cNow = digitalRead(BUTTON_C_PIN) == LOW;

  // A键 - 上一页
  if (aNow && !aPrev) {
    aButtonHeld = true;
    aPressStartTime = now;
  }
  if (!aNow && aPrev) {
    if (now - aPressStartTime < LONG_PRESS_TRIGGER_TIME) {
      currentItemIndex = (currentItemIndex + currentGroup.count - 1) % currentGroup.count;
      displayLabelOnly(currentGroup.items[currentItemIndex]);
      pendingFullRefresh = true;
      lastInteractionTime = now;
    }
    aButtonHeld = false;
  }
  if (aButtonHeld && (now - aPressStartTime >= LONG_PRESS_TRIGGER_TIME)) {
    if (now - aLastRepeatTime >= LONG_PRESS_REPEAT_TIME) {
      currentItemIndex = (currentItemIndex + currentGroup.count - LONG_PRESS_STEP) % currentGroup.count;
      displayLabelOnly(currentGroup.items[currentItemIndex]);
      aLastRepeatTime = now;
      pendingFullRefresh = true;
      lastInteractionTime = now;
    }
  }

  // B键 - 下一页
  if (bNow && !bPrev) {
    bButtonHeld = true;
    bPressStartTime = now;
  }
  if (!bNow && bPrev) {
    if (now - bPressStartTime < LONG_PRESS_TRIGGER_TIME) {
      currentItemIndex = (currentItemIndex + 1) % currentGroup.count;
      displayLabelOnly(currentGroup.items[currentItemIndex]);
      pendingFullRefresh = true;
      lastInteractionTime = now;
    }
    bButtonHeld = false;
  }
  if (bButtonHeld && (now - bPressStartTime >= LONG_PRESS_TRIGGER_TIME)) {
    if (now - bLastRepeatTime >= LONG_PRESS_REPEAT_TIME) {
      currentItemIndex = (currentItemIndex + LONG_PRESS_STEP) % currentGroup.count;
      displayLabelOnly(currentGroup.items[currentItemIndex]);
      bLastRepeatTime = now;
      pendingFullRefresh = true;
      lastInteractionTime = now;
    }
  }

  // C键 - 切换分组
  if (cNow && !cPrev) {
    cButtonHeld = true;
    cPressStartTime = now;
  }
  if (!cNow && cPrev) {
    cButtonHeld = false;
  }
  if (cButtonHeld && (now - cPressStartTime >= GROUP_SWITCH_TRIGGER_TIME)) {
    if (now - cLastRepeatTime >= GROUP_SWITCH_REPEAT_TIME) {
      currentGroupIndex = (currentGroupIndex + 1) % groupCount;
      currentItemIndex = 0;
      displayLabelOnly(groups[currentGroupIndex].items[currentItemIndex]);
      cLastRepeatTime = now;
      pendingFullRefresh = true;
      lastInteractionTime = now;
    }
  }

  aPrev = aNow;
  bPrev = bNow;
  cPrev = cNow;

  // 2秒无操作后进行全刷二维码
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

