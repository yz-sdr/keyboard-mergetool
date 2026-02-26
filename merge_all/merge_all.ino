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

static constexpr uint8_t KEYSET_BYTES = 32;

struct DevState {
  uint8_t keys[KEYSET_BYTES] = {0}; // bitset
};

static DevState s1, s2;
static uint8_t outKeys[KEYSET_BYTES] = {0};

static volatile uint8_t dirtyCount = 0;
static uint32_t lastSendUs = 0;
static const uint32_t SEND_MIN_INTERVAL_US = 1000; // 1ms

// bitset helpers
static inline void setUsage(uint8_t keys[KEYSET_BYTES], uint8_t usage) {
  keys[usage >> 3] |= (uint8_t)(1u << (usage & 7));
}
static inline void clrUsage(uint8_t keys[KEYSET_BYTES], uint8_t usage) {
  keys[usage >> 3] &= (uint8_t)~(1u << (usage & 7));
}
static inline bool hasUsage(const uint8_t keys[KEYSET_BYTES], uint8_t usage) {
  return (keys[usage >> 3] & (uint8_t)(1u << (usage & 7))) != 0;
}

// byte (boot protocol) -> HID usage
static inline uint8_t modBitToUsage(uint8_t bitIndex) {
  return (uint8_t)(0xE0 + bitIndex);
}

static inline KeyboardKeycode usageToKeycode(uint8_t usage) {
  return (KeyboardKeycode)usage;
}

static void syncOutputOnce() {
  uint8_t newKeys[KEYSET_BYTES];

  for (uint8_t i = 0; i < KEYSET_BYTES; i++) {
    newKeys[i] = (uint8_t)(s1.keys[i] | s2.keys[i]);
  }

  bool anyChange = false;
  for (uint8_t byteIdx = 0; byteIdx < KEYSET_BYTES; byteIdx++) {
    uint8_t diff = (uint8_t)(outKeys[byteIdx] ^ newKeys[byteIdx]);
    if (!diff) continue;

    anyChange = true;

    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t mask = (uint8_t)(1u << bit);
      if (!(diff & mask)) continue;

      uint8_t usage = (uint8_t)((byteIdx << 3) | bit);
      if (usage == 0x00) continue; 

      KeyboardKeycode kc = usageToKeycode(usage);

      if (newKeys[byteIdx] & mask) NKROKeyboard.add(kc);
      else                         NKROKeyboard.remove(kc);
    }
  }

  if (anyChange) {
    NKROKeyboard.send();
    for (uint8_t i = 0; i < KEYSET_BYTES; i++) outKeys[i] = newKeys[i];
  }
}

class MergeKbdParser : public KeyboardReportParser {
public:
  explicit MergeKbdParser(DevState &s) : st(s) {}

protected:
  DevState &st;

  void OnControlKeysChanged(uint8_t before, uint8_t after) override {
    uint8_t diff = (uint8_t)(before ^ after);
    if (!diff) return;

    for (uint8_t i = 0; i < 8; i++) {
      uint8_t mask = (uint8_t)(1u << i);
      if (!(diff & mask)) continue;

      uint8_t usage = modBitToUsage(i);

      if (after & mask) {
        if (!hasUsage(st.keys, usage)) {
          setUsage(st.keys, usage);
          dirtyCount++;
        }
      } else {
        if (hasUsage(st.keys, usage)) {
          clrUsage(st.keys, usage);
          dirtyCount++;
        }
      }
    }
  }

  void OnKeyDown(uint8_t mod, uint8_t key) override {
    (void)mod;

    if (key == 0x00) return;

    if (hasUsage(st.keys, key)) return; 
    setUsage(st.keys, key);
    dirtyCount++;
  }

  void OnKeyUp(uint8_t mod, uint8_t key) override {
    (void)mod;

    if (key == 0x00) return;

    if (!hasUsage(st.keys, key)) return; 
    clrUsage(st.keys, key);
    dirtyCount++;
  }
};

MergeKbdParser kbdParser1(s1);
MergeKbdParser kbdParser2(s2);

void setup() {
  Serial.begin(115200);

  if (Usb.Init() == -1) {
    Serial.println("USB Host init failed");
    while (1) {}
  }

  NKROKeyboard.begin();
  NKROKeyboard.releaseAll();
  NKROKeyboard.send();

  Kbd1.SetReportParser(0, &kbdParser1);
  Kbd2.SetReportParser(0, &kbdParser2);

  Serial.println("Ready (Full NKRO merge)");
}

void loop() {
  Usb.Task();

  uint8_t n = dirtyCount;
  if (n) {
    uint32_t now = micros();
    if ((uint32_t)(now - lastSendUs) >= SEND_MIN_INTERVAL_US) {
      dirtyCount = 0;
      syncOutputOnce();
      lastSendUs = now;
    }
  }
}
