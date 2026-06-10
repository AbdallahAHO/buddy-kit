#include "transport_ble.h"
#include "ble_bridge.h"

static size_t _avail(void*) { return bleAvailable(); }
static int    _read(void*)  { return bleRead(); }
static size_t _write(void*, const uint8_t* d, size_t n) { return bleWrite(d, n); }

ByteSource& bleByteSource() {
  static ByteSource s = { _avail, _read, _write, nullptr };
  return s;
}
