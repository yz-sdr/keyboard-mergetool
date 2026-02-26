#include <SPI.h>
#include <Usb.h>
#include <usbhub.h>
#include <hidboot.h>

#include <Keyboard.h>

USB Usb;
USBHub Hub(&Usb);

HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd1(&Usb);
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd2(&Usb);

struct DevState {
  uint8_t mods = 0;
  uint8_t keys[32] = {0};   // 256-bit bitmap
};

static DevState s1, s2;

static uint8_t outMods = 0;
static uint8_t outKeys[32] = {0};

static inline void setBit(uint8_t *bm, uint8_t k)   { bm[k >> 3] |=  (1u << (k & 7)); }
static inline void clrBit(uint8_t *bm, uint8_t k)   { bm[k >> 3] &= ~(1u << (k & 7)); }

static uint8_t modToArduinoKey(uint8_t bitIndex) {
  switch (bitIndex) {
    case 0: return KEY_LEFT_CTRL;
    case 1: return KEY_LEFT_SHIFT;
    case 2: return KEY_LEFT_ALT;
    case 3: return KEY_LEFT_GUI;
    case 4: return KEY_RIGHT_CTRL;
    case 5: return KEY_RIGHT_SHIFT;
    case 6: return KEY_RIGHT_ALT;
    case 7: return KEY_RIGHT_GUI;
  }
  return 0;
}

static uint8_t usageToArduino(uint8_t u) {
  if (u >= 0x04 && u <= 0x1D) return (uint8_t)('a' + (u - 0x04));
  if (u >= 0x1E && u <= 0x26) return (uint8_t)('1' + (u - 0x1E));
  if (u == 0x27) return (uint8_t)('0');

  switch (u) {
    case 0x28: return KEY_RETURN;
    case 0x29: return KEY_ESC;
    case 0x2A: return KEY_BACKSPACE;
    case 0x2B: return KEY_TAB;
    case 0x2C: return (uint8_t)' ';

    case 0x2D: return (uint8_t)'-';
    case 0x2E: return (uint8_t)'=';
    case 0x2F: return (uint8_t)'[';
    case 0x30: return (uint8_t)']';
    case 0x31: return (uint8_t)'\\';
    case 0x33: return (uint8_t)';';
    case 0x34: return (uint8_t)'\'';
    case 0x35: return (uint8_t)'`';
    case 0x36: return (uint8_t)',';
    case 0x37: return (uint8_t)'.';
    case 0x38: return (uint8_t)'/';

    case 0x39: return KEY_CAPS_LOCK;
    case 0x3A: return KEY_F1;
    case 0x3B: return KEY_F2;
    case 0x3C: return KEY_F3;
    case 0x3D: return KEY_F4;
    case 0x3E: return KEY_F5;
    case 0x3F: return KEY_F6;
    case 0x40: return KEY_F7;
    case 0x41: return KEY_F8;
    case 0x42: return KEY_F9;
    case 0x43: return KEY_F10;
    case 0x44: return KEY_F11;
    case 0x45: return KEY_F12;

    case 0x49: return KEY_INSERT;
    case 0x4A: return KEY_HOME;
    case 0x4B: return KEY_PAGE_UP;
    case 0x4C: return KEY_DELETE;
    case 0x4D: return KEY_END;
    case 0x4E: return KEY_PAGE_DOWN;
    case 0x4F: return KEY_RIGHT_ARROW;
    case 0x50: return KEY_LEFT_ARROW;
    case 0x51: return KEY_DOWN_ARROW;
    case 0x52: return KEY_UP_ARROW;
  }
  return 0;
}

static inline void pressUsage(uint8_t usage) {
  uint8_t k = usageToArduino(usage);
  if (k) Keyboard.press(k);
}

static inline void releaseUsage(uint8_t usage) {
  uint8_t k = usageToArduino(usage);
  if (k) Keyboard.release(k);
}


static void syncOutput() {
  // 1) mods
  uint8_t newMods = s1.mods | s2.mods;
  uint8_t diffM = outMods ^ newMods;
  if (diffM) {
    for (uint8_t b = 0; b < 8; b++) {
      uint8_t mask = (1u << b);
      if (!(diffM & mask)) continue;

      uint8_t mk = modToArduinoKey(b);
      if (!mk) continue;

      if (newMods & mask) Keyboard.press(mk);
      else                Keyboard.release(mk);
    }
    outMods = newMods;
  }

  // 2) keys
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t newByte = s1.keys[i] | s2.keys[i];
    uint8_t diff = outKeys[i] ^ newByte;
    if (!diff) continue;


    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t mask = (1u << bit);
      if (!(diff & mask)) continue;

      uint8_t usage = (uint8_t)((i << 3) | bit);


      if (usage >= 0xE0 && usage <= 0xE7) continue;

      if (newByte & mask) pressUsage(usage);
      else                releaseUsage(usage);
    }

    outKeys[i] = newByte;
  }
}

class MergeKbdParser : public KeyboardReportParser {
public:
  explicit MergeKbdParser(DevState &s) : st(s) {}

protected:
  DevState &st;

  void OnControlKeysChanged(uint8_t before, uint8_t after) override {
    (void)before;
    st.mods = after;
    syncOutput();
  }

  void OnKeyDown(uint8_t mod, uint8_t key) override {
    (void)mod;
    if (key) setBit(st.keys, key);
    syncOutput();
  }

  void OnKeyUp(uint8_t mod, uint8_t key) override {
    (void)mod;
    if (key) clrBit(st.keys, key);
    syncOutput();
  }
};

MergeKbdParser kbdParser1(s1);
MergeKbdParser kbdParser2(s2);

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  if (Usb.Init() == -1) {
    while (1) {}
  }

  Keyboard.begin();

  Kbd1.SetReportParser(0, &kbdParser1);
  Kbd2.SetReportParser(0, &kbdParser2);
}

void loop() {
  Usb.Task();
}
