/**
 * Morse Code Trainer for M5Stack StickS3
 *
 * A binary-tree based Morse code learning tool optimized for the
 * StickS3's 128×128 pixel LCD.
 *
 * StickS3 display notes (from official M5Unified HowToUse example):
 * - The ST7789 panel is 135×240; the visible lens is 128×128
 * - setRotation(1) gives landscape orientation (logical 240×135)
 * - M5GFX applies internal offsets (52, 40) to center the 128×128
 *   viewport within the panel
 * - Effective drawable area: (0,0) to (127,127) in user coordinates
 * - Text size formula from official example:
 *     textsize = M5.Display.height() / 160 (→ 1 for StickS3)
 *   This is too small for dense UI; we use textsize=2 for readability
 *
 * Controls:
 *   Short press (< 380 ms):  Dot (left tree branch)
 *   Long press  (≥ 380 ms):  Dash (right tree branch)
 *   Auto-reset after 2200 ms of inactivity
 */

#include <M5Unified.h>
#include "hal/usb_serial_jtag_ll.h"

namespace {

// ── Display ────────────────────────────────────────────────────────
constexpr int kScreenW = 128;
constexpr int kScreenH = 128;
constexpr uint8_t kDisplayRotation = 1;  // landscape per official example
constexpr int kTextSize = 2;             // readable on 128×128

// ── Tree ───────────────────────────────────────────────────────────
constexpr int kMaxDepth = 4;             // 4 levels = 31 nodes max
constexpr int kMaxNodes = 31;

// ── Timing (ms) ────────────────────────────────────────────────────
constexpr uint32_t kLongPressMs  = 380;
constexpr uint32_t kIdleResetMs  = 2200;
constexpr uint32_t kFlashMs      = 700;
constexpr uint32_t kDotMs        = 90;
constexpr uint32_t kDashMs       = 270;
constexpr float    kToneHz       = 880.0f;

// ── Colors (RGB565) ────────────────────────────────────────────────
constexpr uint16_t kBg          = 0x10A4;
constexpr uint16_t kTrace       = 0xEF7D;
constexpr uint16_t kTraceActive = 0x06FF;
constexpr uint16_t kNodeFill    = 0x18E7;
constexpr uint16_t kNodeActive  = 0xFD20;
constexpr uint16_t kTextWhite   = TFT_WHITE;
constexpr uint16_t kTextDim     = 0xC638;

// ── Data ───────────────────────────────────────────────────────────
struct MorseEntry {
  char letter;
  const char* code;
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

struct TreeNode {
  bool occupied = false;
  char letter   = '\0';
};

// Node positions on 128×128 grid — arranged for full-screen tree
// Index = heap index (root=1, left=idx*2, right=idx*2+1)
struct NodeVisual {
  int16_t x;
  int16_t y;
};

// 4-level binary tree layout using the FULL 128×128 area
// Level 0 (root):     1 node  at y=102
// Level 1:            2 nodes at y=80
// Level 2:            4 nodes at y=56
// Level 3:            8 nodes at y=28
// Level 4 (leaves):  16 nodes at y=6  (just dots, no circles)
// Total: 31 positions
constexpr NodeVisual kLayout[32] = {
  // idx 0 (unused)
  {0,   0},
  // L0: root
  {64, 115},
  // L1: 2 nodes
  {36, 92}, {92, 92},
  // L2: 4 nodes
  {20, 66},  {52, 66},  {76, 66},  {108, 66},
  // L3: 8 nodes
  {12, 38},  {28, 38},  {40, 38},  {64, 38},
  {76, 38},  {92, 38},  {104, 38}, {116, 38},
  // L4: 16 leaf positions (labels only, no circles)
  {6,   8},  {14,  8},  {22,  8},  {31,  8},
  {41,  8},  {48,  8},  {56,  8},  {64,  8},
  {73,  8},  {80,  8},  {87,  8},  {96,  8},
  {105, 8},  {112, 8},  {118, 8},  {124, 8},
};

// ── State ──────────────────────────────────────────────────────────
TreeNode g_tree[1 << (kMaxDepth + 1)];
int      g_currentIndex = 1;
bool     g_buttonDown   = false;
uint32_t g_buttonDownAt = 0;
uint32_t g_lastActionAt = 0;
uint32_t g_flashUntil   = 0;
bool     g_lastMoveValid = true;
const char* g_flashText = "Ready";

// ── Helpers ────────────────────────────────────────────────────────
int childIndex(int node, bool goLeft) {
  return node * 2 + (goLeft ? 0 : 1);
}

int nodeDepth(int index) {
  int d = 0;
  while (index > 1) { index /= 2; ++d; }
  return d;
}

bool hasNode(int index) {
  return index > 0 && index <= kMaxNodes && g_tree[index].occupied;
}

bool isOnCurrentPath(int index) {
  for (int probe = g_currentIndex; probe >= 1; probe /= 2) {
    if (probe == index) return true;
  }
  return false;
}

void markPath(int index, char* out) {
  int d = nodeDepth(index);
  out[d] = '\0';
  while (d > 0) {
    out[d - 1] = (index % 2 == 0) ? '-' : '.';
    index /= 2; --d;
  }
}

const char* codeText(int index) {
  static char path[8];
  if (index == 1) return "*";
  markPath(index, path);
  return path;
}

const char* nodeLabel(int index) {
  static char label[2];
  label[0] = g_tree[index].letter ? g_tree[index].letter : '*';
  label[1] = '\0';
  return label;
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

}  // namespace

// ── Hardware setup ─────────────────────────────────────────────────────
static void forceUsbReenumeration() {
  USB_SERIAL_JTAG.conf0.dp_pullup = 0;
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 0;
  delay(200);
  USB_SERIAL_JTAG.conf0.dp_pullup = 1;
  USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
  delay(200);
}

// ── Audio / feedback ───────────────────────────────────────────────────
static void showFlash(const char* text, bool valid) {
  g_flashText    = text;
  g_lastMoveValid = valid;
  g_flashUntil    = millis() + kFlashMs;
}

static void playElementTone(bool dash) {
  M5.Speaker.tone(kToneHz, dash ? kDashMs : kDotMs);
}

// ── Drawing ────────────────────────────────────────────────────────────
static void drawSegment(int x0, int y0, int x1, int y1, uint16_t c) {
  // 2px thick lines for visibility on small screen
  M5.Display.drawLine(x0, y0, x1, y1, c);
  M5.Display.drawLine(x0 + 1, y0, x1 + 1, y1, c);
}

static void drawConnection(int parent, int child, uint16_t color) {
  const auto& p = kLayout[parent];
  const auto& c = kLayout[child];
  int midY = (p.y + c.y) / 2;
  drawSegment(p.x, p.y, p.x, midY, color);
  drawSegment(p.x, midY, c.x, midY, color);
  drawSegment(c.x, midY, c.x, c.y, color);
}

static void drawBoardFrame() {
  // Full-screen rounded border using all 128×128 pixels
  M5.Display.fillRoundRect(1, 1, 126, 126, 14, kBg);
  M5.Display.drawRoundRect(1, 1, 126, 126, 14, 0xBDF7);
}

static void drawHeader() {
  // Compact header: minimal height, centered at top
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextColor(kTextDim, kBg);
  M5.Display.drawString("MORSE   Dash=long  Dot=short", 64, 4);
}

static void drawTraces() {
  for (int index = 1; index <= 15; ++index) {
    if (!hasNode(index)) continue;
    int left  = childIndex(index, true);
    int right = childIndex(index, false);
    if (hasNode(left))  drawConnection(index, left,  isOnCurrentPath(left)  ? kTraceActive : kTrace);
    if (hasNode(right)) drawConnection(index, right, isOnCurrentPath(right) ? kTraceActive : kTrace);
  }
}

static void drawNode(int index) {
  if (!hasNode(index)) return;
  bool isLeaf = nodeDepth(index) >= kMaxDepth;

  const auto& n = kLayout[index];
  bool active = (index == g_currentIndex);
  bool path   = isOnCurrentPath(index);

  if (isLeaf) {
    // Leaves: just the letter label, no circle (saves space)
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(middle_center);
    if (active) {
      M5.Display.setTextColor(kNodeActive, kBg);
    } else {
      M5.Display.setTextColor(path ? kTraceActive : kTrace, kBg);
    }
    M5.Display.drawString(nodeLabel(index), n.x, n.y);
  } else {
    // Internal nodes: filled circle + label
    int r = active ? 6 : 5;
    uint16_t fill = active ? kNodeActive : kNodeFill;
    uint16_t ring = active ? TFT_WHITE : (path ? kTraceActive : kTrace);

    M5.Display.fillCircle(n.x, n.y, r, fill);
    M5.Display.drawCircle(n.x, n.y, r, ring);

    // Label inside circle
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(active ? TFT_BLACK : kTextWhite, fill);
    M5.Display.drawString(nodeLabel(index), n.x, n.y);
  }
}

static void drawNodes() {
  for (int index = 1; index <= kMaxNodes; ++index) drawNode(index);
}

static void drawStatusBar() {
  char path[8];
  markPath(g_currentIndex, path);
  int left  = childIndex(g_currentIndex, true);
  int right = childIndex(g_currentIndex, false);

  // Status bar at very bottom of screen
  int barY = kScreenH - 10;
  M5.Display.fillRoundRect(4, barY, 120, 9, 4, 0x18C6);
  M5.Display.drawRoundRect(4, barY, 120, 9, 4, 0x7BEF);

  // Current code (left side)
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(kTextWhite, 0x18C6);
  M5.Display.setCursor(7, barY + 1);
  if (g_currentIndex == 1) {
    M5.Display.print("*");
  } else if (g_tree[g_currentIndex].letter) {
    M5.Display.printf("%c %s", g_tree[g_currentIndex].letter, path);
  } else {
    M5.Display.printf("%s", path);
  }

  // Available branches (center)
  M5.Display.setTextColor(kTextDim, 0x18C6);
  M5.Display.setTextDatum(top_center);
  M5.Display.drawString(
    (String("-") + (hasNode(left)  ? nodeLabel(left)  : "_") +
     "  ."  + (hasNode(right) ? nodeLabel(right) : "_")).c_str(),
    80, barY + 1);

  // Flash feedback (right side)
  if (millis() < g_flashUntil) {
    M5.Display.setTextDatum(top_right);
    M5.Display.setTextColor(g_lastMoveValid ? TFT_GREEN : TFT_RED, 0x18C6);
    M5.Display.drawString(g_flashText, 122, barY + 1);
  }
}

static void renderUi() {
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  drawBoardFrame();
  drawHeader();
  drawTraces();
  drawNodes();
  drawStatusBar();
  M5.Display.endWrite();
}

// ── Input handling ─────────────────────────────────────────────────────
static void moveSelection(bool goLeft) {
  int next = childIndex(g_currentIndex, goLeft);
  playElementTone(goLeft);
  if (hasNode(next)) {
    g_currentIndex = next;
    g_lastActionAt = millis();
    showFlash(goLeft ? "Dash" : "Dot", true);
  } else {
    showFlash("No branch", false);
  }
  M5.Power.setLed(g_currentIndex == 1 ? 0 : 1);
  renderUi();
}

static void resetToRoot(const char* reason) {
  g_currentIndex = 1;
  g_lastActionAt = millis();
  showFlash(reason, true);
  M5.Power.setLed(0);
  renderUi();
}

static void handleButton() {
  bool pressed = M5.BtnA.isPressed();
  uint32_t now = millis();

  if (pressed && !g_buttonDown) {
    g_buttonDown    = true;
    g_buttonDownAt  = now;
  } else if (!pressed && g_buttonDown) {
    uint32_t duration = now - g_buttonDownAt;
    g_buttonDown = false;
    moveSelection(duration >= kLongPressMs);
  }
}

static void handleIdleReset() {
  if (g_currentIndex != 1 && millis() - g_lastActionAt > kIdleResetMs) {
    resetToRoot("Auto");
  }
}

// ── Arduino entry ──────────────────────────────────────────────────────
void setup() {
  forceUsbReenumeration();

  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== StickS3 Morse Trainer ===");

  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);

  // StickS3 correct orientation: rotation=0 gives portrait (135×240 panel,
  // 128×128 visible lens centered within).  The official HowToUse auto-rotate
  // to landscape is NOT right for this board — it causes the content to fill
  // only half the visible area.
  M5.Display.setRotation(0);

  M5.Display.setTextSize(kTextSize);
  M5.Speaker.setVolume(96);

  Serial.printf("Board: %s  Rotation=%u  W=%d H=%d\n",
                M5.getBoard() == m5::board_t::board_M5StickS3 ? "StickS3" : "?",
                M5.Display.getRotation(),
                M5.Display.width(),
                M5.Display.height());

  buildTree();
  g_lastActionAt = millis();
  showFlash("Ready", true);
  M5.Power.setLed(0);
  renderUi();
}

void loop() {
  M5.update();
  handleButton();
  handleIdleReset();
  delay(10);
}
