#include <M5Unified.h>
#include "hal/usb_serial_jtag_ll.h"

namespace {

constexpr int kScreenW = 128;
constexpr int kScreenH = 128;
constexpr int kPanelTop = 92;
constexpr int kTreeLeft = 8;
constexpr int kTreeRight = 120;
constexpr int kMaxDepth = 4;
constexpr int kLeafSlots = 1 << kMaxDepth;
constexpr uint32_t kLongPressMs = 380;
constexpr uint32_t kIdleResetMs = 2200;
constexpr uint32_t kFlashMs = 700;

struct MorseEntry {
  char letter;
  const char* code;
};

struct TreeNode {
  bool occupied = false;
  char letter = '\0';
};

constexpr MorseEntry kAlphabet[] = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},
    {'E', "."},    {'F', "..-."}, {'G', "--."},  {'H', "...."},
    {'I', ".."},   {'J', ".---"}, {'K', "-.-"},  {'L', ".-.."},
    {'M', "--"},   {'N', "-."},   {'O', "---"},  {'P', ".--."},
    {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"},
    {'Y', "-.--"}, {'Z', "--.."},
};

TreeNode g_tree[1 << (kMaxDepth + 1)];
int g_currentIndex = 1;
bool g_buttonDown = false;
uint32_t g_buttonDownAt = 0;
uint32_t g_lastActionAt = 0;
uint32_t g_flashUntil = 0;
bool g_lastMoveValid = true;
const char* g_flashText = "Ready";

int childIndex(int node, bool goLeft) {
  return node * 2 + (goLeft ? 0 : 1);
}

int nodeDepth(int index) {
  int depth = 0;
  while (index > 1) {
    index /= 2;
    ++depth;
  }
  return depth;
}

int nodeBits(int index) {
  int depth = nodeDepth(index);
  return index - (1 << depth);
}

float nodeX(int index) {
  int depth = nodeDepth(index);
  int bits = nodeBits(index);
  int width = 1 << (kMaxDepth - depth);
  float center = bits * width + width * 0.5f;
  float span = static_cast<float>(kTreeRight - kTreeLeft);
  return kTreeLeft + span * (center / kLeafSlots);
}

float nodeY(int index) {
  int depth = nodeDepth(index);
  return 12.0f + depth * 18.0f;
}

bool hasNode(int index) {
  return index < static_cast<int>(sizeof(g_tree) / sizeof(g_tree[0])) &&
         g_tree[index].occupied;
}

const char* letterText(int index) {
  if (!hasNode(index)) {
    return "--";
  }
  if (index == 1) {
    return "ROOT";
  }
  static char buf[2];
  buf[0] = g_tree[index].letter ? g_tree[index].letter : '*';
  buf[1] = '\0';
  return buf;
}

void markPath(int index, char* out) {
  int depth = nodeDepth(index);
  out[depth] = '\0';
  while (depth > 0) {
    out[depth - 1] = (index % 2 == 0) ? '-' : '.';
    index /= 2;
    --depth;
  }
}

void buildTree() {
  g_tree[1].occupied = true;
  for (const auto& entry : kAlphabet) {
    int index = 1;
    g_tree[index].occupied = true;
    for (const char* p = entry.code; *p; ++p) {
      index = childIndex(index, *p == '-');
      g_tree[index].occupied = true;
    }
    g_tree[index].letter = entry.letter;
  }
}

void forceUsbReenumeration() {
  USB_SERIAL_JTAG.conf0.dp_pullup = 0;
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 0;
  delay(200);
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
  delay(200);
}

void showFlash(const char* text, bool valid) {
  g_flashText = text;
  g_lastMoveValid = valid;
  g_flashUntil = millis() + kFlashMs;
}

void drawHeader() {
  M5.Lcd.setTextDatum(top_center);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(0xFFE0, TFT_BLACK);
  M5.Lcd.drawString("Morse Tree Trainer", kScreenW / 2, 1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.drawString("Long = left / Short = right", kScreenW / 2, 10);
}

void drawConnections() {
  for (int index = 1; index < 16; ++index) {
    if (!hasNode(index)) {
      continue;
    }
    int left = childIndex(index, true);
    int right = childIndex(index, false);
    float x1 = nodeX(index);
    float y1 = nodeY(index);
    if (hasNode(left)) {
      M5.Lcd.drawLine(static_cast<int>(x1), static_cast<int>(y1),
                      static_cast<int>(nodeX(left)), static_cast<int>(nodeY(left)),
                      TFT_DARKGREY);
    }
    if (hasNode(right)) {
      M5.Lcd.drawLine(static_cast<int>(x1), static_cast<int>(y1),
                      static_cast<int>(nodeX(right)), static_cast<int>(nodeY(right)),
                      TFT_DARKGREY);
    }
  }
}

void drawNodes() {
  for (int index = 1; index < 32; ++index) {
    if (!hasNode(index)) {
      continue;
    }

    int x = static_cast<int>(nodeX(index));
    int y = static_cast<int>(nodeY(index));
    bool active = (index == g_currentIndex);
    bool onPath = false;
    for (int probe = g_currentIndex; probe >= 1; probe /= 2) {
      if (probe == index) {
        onPath = true;
        break;
      }
    }

    uint16_t fill = TFT_BLACK;
    uint16_t ring = TFT_DARKGREY;
    uint16_t text = TFT_WHITE;
    int radius = 5;

    if (index == 1) {
      fill = 0x18C3;
      text = TFT_CYAN;
    }
    if (onPath) {
      ring = 0x7BEF;
    }
    if (active) {
      fill = 0xFD20;
      ring = TFT_WHITE;
      text = TFT_BLACK;
      radius = 6;
    }

    M5.Lcd.fillCircle(x, y, radius, fill);
    M5.Lcd.drawCircle(x, y, radius, ring);

    if (g_tree[index].letter) {
      char label[2] = {g_tree[index].letter, '\0'};
      M5.Lcd.setTextDatum(middle_center);
      M5.Lcd.setTextColor(text, fill);
      M5.Lcd.drawString(label, x, y + 1);
    } else if (index == 1) {
      M5.Lcd.setTextDatum(middle_center);
      M5.Lcd.setTextColor(text, fill);
      M5.Lcd.drawString("*", x, y + 1);
    }
  }
}

void drawPanel() {
  char path[8];
  markPath(g_currentIndex, path);

  int left = childIndex(g_currentIndex, true);
  int right = childIndex(g_currentIndex, false);

  M5.Lcd.fillRoundRect(4, kPanelTop, 120, 32, 6, 0x1061);
  M5.Lcd.drawRoundRect(4, kPanelTop, 120, 32, 6, 0x39E7);

  M5.Lcd.setTextDatum(top_left);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE, 0x1061);
  M5.Lcd.setCursor(8, kPanelTop + 4);
  M5.Lcd.printf("Path:%s", path[0] ? path : " root");

  M5.Lcd.setTextColor(0xFFE0, 0x1061);
  M5.Lcd.setCursor(8, kPanelTop + 14);
  if (g_currentIndex == 1) {
    M5.Lcd.print("Current: start");
  } else if (g_tree[g_currentIndex].letter) {
    M5.Lcd.printf("Current: %c", g_tree[g_currentIndex].letter);
  } else {
    M5.Lcd.print("Current: branch");
  }

  M5.Lcd.setTextColor(TFT_CYAN, 0x1061);
  M5.Lcd.setCursor(8, kPanelTop + 24);
  M5.Lcd.printf("L:%s  R:%s", letterText(left), letterText(right));

  uint16_t flashColor = g_lastMoveValid ? TFT_GREEN : TFT_RED;
  if (millis() < g_flashUntil) {
    M5.Lcd.setTextDatum(top_right);
    M5.Lcd.setTextColor(flashColor, 0x1061);
    M5.Lcd.drawString(g_flashText, 120, kPanelTop + 4);
  }
}

void renderUi() {
  M5.Lcd.startWrite();
  M5.Lcd.fillScreen(TFT_BLACK);
  drawHeader();
  drawConnections();
  drawNodes();
  drawPanel();
  M5.Lcd.endWrite();
}

void setLedForDepth() {
  int depth = nodeDepth(g_currentIndex);
  M5.Power.setLed(depth > 0 ? 1 : 0);
}

void moveSelection(bool goLeft) {
  int next = childIndex(g_currentIndex, goLeft);
  if (hasNode(next)) {
    g_currentIndex = next;
    g_lastActionAt = millis();
    showFlash(goLeft ? "Long -> left" : "Short -> right", true);
  } else {
    showFlash("No branch", false);
  }
  setLedForDepth();
  renderUi();
}

void resetToRoot(const char* reason) {
  g_currentIndex = 1;
  g_lastActionAt = millis();
  showFlash(reason, true);
  setLedForDepth();
  renderUi();
}

void handleButton() {
  bool pressed = M5.BtnA.isPressed();
  uint32_t now = millis();

  if (pressed && !g_buttonDown) {
    g_buttonDown = true;
    g_buttonDownAt = now;
  } else if (!pressed && g_buttonDown) {
    uint32_t duration = now - g_buttonDownAt;
    g_buttonDown = false;
    moveSelection(duration >= kLongPressMs);
  }
}

void handleIdleReset() {
  if (g_currentIndex != 1 && millis() - g_lastActionAt > kIdleResetMs) {
    resetToRoot("Auto reset");
  }
}

}  // namespace

void setup() {
  forceUsbReenumeration();

  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== StickS3 Morse Trainer ===");

  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  buildTree();
  g_lastActionAt = millis();
  showFlash("Ready", true);
  setLedForDepth();
  renderUi();
}

void loop() {
  M5.update();
  handleButton();
  handleIdleReset();
  delay(10);
}
