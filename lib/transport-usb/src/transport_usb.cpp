#include "transport_usb.h"
#include <Arduino.h>

static size_t _avail(void*) { return (size_t)Serial.available(); }
static int    _read(void*)  { return Serial.read(); }
static size_t _write(void*, const uint8_t* d, size_t n) { return Serial.write(d, n); }

ByteSource& usbSerialSource() {
  static ByteSource s = { _avail, _read, _write, nullptr };
  return s;
}
