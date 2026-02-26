#include <SPI.h>
#include <Usb.h>
#include <usbhub.h>
#include <hidboot.h>

#include <HID-Project.h>
#include <HID-Settings.h>

USB Usb;
USBHub Hub(&Usb);

HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd1(&Usb);
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> Kbd2(&Usb);

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
  uint16_t mask = 0;
};

static DevState s1, s2;
static uint16_t outMask = 0;
static volatile bool dirty = false;

static inline void setKey(uint16_t &m, uint8_t bit) { m |=  (uint16_t(1) << bit); }
static inline void clrKey(uint16_t &m, uint8_t bit) { m &= ~(uint16_t(1) << bit); }

// Host：HID usage
static uint8_t usageToBit(uint8_t u) {
  switch (u) {
    case 0x1D: return K_Z;     // Z
    case 0x1B: return K_X;     // X
    case 0x06: return K_C;     // C
    case 0x19: return K_V;     // V
    case 0x29: return K_ESC;   // Esc
    case 0x05: return K_B;     // B
    case 0x07: return K_D;     // D

    case 0x4B: return K_PGUP;  // Page Up
    case 0x4E: return K_PGDN;  // Page Down

    case 0x52: return K_UP;    // Up
    case 0x51: return K_DOWN;  // Down
    case 0x50: return K_LEFT;  // Left
    case 0x4F: return K_RIGHT; // Right
  }
  return 0xFF;
}

// HID-Project KeyboardKeycode
static bool bitToKeycode(uint8_t bit, KeyboardKeycode &out) {
  switch (bit) {
    case K_Z:     out = KEY_Z; break;
    case K_X:     out = KEY_X; break;
    case K_C:     out = KEY_C; break;
    case K_V:     out = KEY_V; break;
    case K_ESC:   out = KEY_ESC; break;
    case K_B:     out = KEY_B; break;
    case K_D:     out = KEY_D; break;

    case K_PGUP:  out = KEY_PAGE_UP; break;
    case K_PGDN:  out = KEY_PAGE_DOWN; break;

    case K_UP:    out = KEY_UP_ARROW; break;
    case K_DOWN:  out = KEY_DOWN_ARROW; break;
    case K_LEFT:  out = KEY_LEFT_ARROW; break;
    case K_RIGHT: out = KEY_RIGHT_ARROW; break;

    default:
      return false;
  }
  return true;
}


static void syncOutputOnce() {
  uint16_t newMask = s1.mask | s2.mask;
  uint16_t diff = outMask ^ newMask;
  if (!diff) return;

  for (uint8_t b = 0; b < K_COUNT; b++) {
    uint16_t bit = (uint16_t(1) << b);
    if (!(diff & bit)) continue;

    KeyboardKeycode kc;
    if (!bitToKeycode(b, kc)) continue;

    if (newMask & bit) NKROKeyboard.add(kc);
    else               NKROKeyboard.remove(kc);
  }

  NKROKeyboard.send();
  outMask = newMask;
}


class MergeKbdParser : public KeyboardReportParser {
public:
  explicit MergeKbdParser(DevState &s) : st(s) {}

protected:
  DevState &st;

  void OnControlKeysChanged(uint8_t before, uint8_t after) override {
    (void)before; (void)after;
  }

  void OnKeyDown(uint8_t mod, uint8_t key) override {
    (void)mod;
    uint8_t b = usageToBit(key);
    if (b == 0xFF) return;
    setKey(st.mask, b);
    dirty = true;       
  }

  void OnKeyUp(uint8_t mod, uint8_t key) override {
    (void)mod;
    uint8_t b = usageToBit(key);
    if (b == 0xFF) return;
    clrKey(st.mask, b);
    dirty = true;
  }
};

MergeKbdParser kbdParser1(s1);
MergeKbdParser kbdParser2(s2);

void setup() {
  Serial.begin(115200);
  // if (false) while (!Serial) {}

  if (Usb.Init() == -1) {
    Serial.println("USB Host init failed");
    while (1) {}
  }

  NKROKeyboard.begin();
  NKROKeyboard.releaseAll();
  NKROKeyboard.send(); 

  Kbd1.SetReportParser(0, &kbdParser1);
  Kbd2.SetReportParser(0, &kbdParser2);

  Serial.println("Ready (NKRO merge)");
}

void loop() {
  Usb.Task();

  if (dirty) {
    dirty = false;
    syncOutputOnce();
  }
}
