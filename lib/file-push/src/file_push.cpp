#include "file_push.h"
#include <string.h>
#include <stdio.h>
#include <mbedtls/base64.h>

static const FileSink* _sink = nullptr;
static LineOut*        _out  = nullptr;

static uint32_t _expected = 0, _written = 0;
static char     _setName[24] = "";
static bool     _active = false, _fileOk = false;
static uint32_t _total = 0, _totalWritten = 0;

void filePushInit(const FileSink* sink, LineOut* out) { _sink = sink; _out = out; }

static void _ack(const char* what, bool ok, uint32_t n = 0) {
  lineBusAck(*_out, what, ok, n);
}

bool filePushHandle(JsonDocument& doc) {
  const char* cmd = doc["cmd"];
  if (!cmd || !_sink || !_out) return false;

  if (strcmp(cmd, "char_begin") == 0) {
    const char* name = doc["name"] | "pet";
    _total = doc["total"] | 0;
    uint32_t avail = 0;
    char err[48] = "";
    if (!_sink->begin(_sink->user, name, _total, &avail, err, sizeof(err))) {
      char b[96];
      int len = snprintf(b, sizeof(b),
        "{\"ack\":\"char_begin\",\"ok\":false,\"n\":%lu,\"error\":\"%s\"}\n",
        (unsigned long)avail, err);
      _out->write((const uint8_t*)b, len);
      return true;
    }
    strncpy(_setName, name, sizeof(_setName)-1); _setName[sizeof(_setName)-1] = 0;
    _totalWritten = 0;
    _active = true;
    _ack("char_begin", true);
    return true;
  }

  if (!_active) return false;   // caller decides what inactive cmds mean

  if (strcmp(cmd, "file") == 0) {
    const char* path = doc["path"];
    _expected = doc["size"] | 0;
    _written = 0;
    if (!path) { _ack("file", false); return true; }
    _fileOk = _sink->open(_sink->user, path);
    _ack("file", _fileOk);
    return true;
  }

  if (strcmp(cmd, "chunk") == 0) {
    const char* b64 = doc["d"];
    if (!b64 || !_fileOk) { _ack("chunk", false); return true; }
    uint8_t buf[300];
    size_t outLen = 0;
    int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                   (const uint8_t*)b64, strlen(b64));
    if (rc != 0) { _ack("chunk", false); return true; }
    _sink->write(_sink->user, buf, outLen);
    _written += outLen;
    _totalWritten += outLen;
    // Ack every chunk — this is the flow control (see header).
    _ack("chunk", true, _written);
    return true;
  }

  if (strcmp(cmd, "file_end") == 0) {
    bool ok = _fileOk && (_written == _expected || _expected == 0);
    if (_fileOk) _sink->close(_sink->user);
    _fileOk = false;
    _ack("file_end", ok, _written);
    return true;
  }

  if (strcmp(cmd, "char_end") == 0) {
    _active = false;
    bool ok = _sink->finish(_sink->user, _setName);
    _ack("char_end", ok);
    return true;
  }

  return false;
}

bool     filePushActive()   { return _active; }
uint32_t filePushProgress() { return _totalWritten; }
uint32_t filePushTotal()    { return _total; }
