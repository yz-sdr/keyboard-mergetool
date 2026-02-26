#include <SPI.h>
#include <Usb.h>
#include <usbhub.h>
#include <hidboot.h>

#include <Keyboard.h>

USB Usb;
USBHub Hub(&Usb);

HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd1(&Usb);
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd2(&Usb);

// bit def
enum : uint8_t {
  K_Z = 0,
  K_X,
  K_C,
  K_V,
  K_ESC,
  K_B,
  K_D,
  K_PGUP,
  K_PGDN,
  K_UP,
  K_DOWN,
  K_LEFT,
  K_RIGHT,
  K_COUNT
};

struct DevState {
  // uint8_t mods = 0;     // Shift/Ctrl
  uint16_t mask = 0;    // 12-bit keys
};

static DevState s1, s2;

// static uint8_t outMods = 0;
static uint16_t outMask = 0;

static inline void setKey(uint16_t &m, uint8_t bit) { m |=  (uint16_t(1) << bit); }
static inline void clrKey(uint16_t &m, uint8_t bit) { m &= ~(uint16_t(1) << bit); }

// HID usage
static uint8_t usageToBit(uint8_t u) {
  switch (u) {

    // Z=0x1D, X=0x1B, C=0x06, V=0x19, B=0x05, D=0x07
    case 0x1D: return K_Z;
    case 0x1B: return K_X;
    case 0x06: return K_C;
    case 0x19: return K_V;
    case 0x29: return K_ESC;
    case 0x05: return K_B;
    case 0x07: return K_D;

    case 0x4B: return K_PGUP;   
    case 0x4E: return K_PGDN;  
    case 0x52: return K_UP;
    case 0x51: return K_DOWN;
    case 0x50: return K_LEFT;
    case 0x4F: return K_RIGHT;
  }
  return 0xFF;
}

// bit -> Arduino Keyboard.press/release
static uint8_t bitToArduinoKey(uint8_t bit) {
  switch (bit) {
    case K_Z:    return 'z';
    case K_X:    return 'x';
    case K_C:    return 'c';
    case K_V:    return 'v';
    case K_ESC:  return KEY_ESC;
    case K_B:    return 'b';
    case K_D:    return 'd';

    case K_PGUP: return KEY_PAGE_UP;
    case K_PGDN: return KEY_PAGE_DOWN;

    case K_UP:    return KEY_UP_ARROW;
    case K_DOWN:  return KEY_DOWN_ARROW;
    case K_LEFT:  return KEY_LEFT_ARROW;
    case K_RIGHT: return KEY_RIGHT_ARROW;
  }
  return 0;
}

// mods bit -> Arduino key
/*
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
*/
static void syncOutput() {
  // mods
  /*
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
  */

  // mask
  uint16_t newMask = s1.mask | s2.mask;
  uint16_t diff = outMask ^ newMask;
  if (!diff) return;

  for (uint8_t b = 0; b < K_COUNT; b++) {
    uint16_t bit = (uint16_t(1) << b);
    if (!(diff & bit)) continue;

    uint8_t key = bitToArduinoKey(b);
    if (!key) continue;

    if (newMask & bit) Keyboard.press(key);
    else               Keyboard.release(key);
  }

  outMask = newMask;
}

class MergeKbdParser : public KeyboardReportParser {
public:
  explicit MergeKbdParser(DevState &s) : st(s) {}

protected:
  DevState &st;
  /*
  void OnControlKeysChanged(uint8_t before, uint8_t after) override {
    (void)before;
    st.mods = after;
    syncOutput();
  }
  */

  void OnKeyDown(uint8_t mod, uint8_t key) override {
    (void)mod;
    uint8_t b = usageToBit(key);
    if (b != 0xFF) {
      setKey(st.mask, b);
      syncOutput();
    }
  }

  void OnKeyUp(uint8_t mod, uint8_t key) override {
    (void)mod;
    uint8_t b = usageToBit(key);
    if (b != 0xFF) {
      clrKey(st.mask, b);
      syncOutput();
    }
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

  Serial.println("Ready");
}

void loop() {
  Usb.Task();
}
