#pragma once
#include <stdint.h>
#include <stddef.h>

// Device side of notify's pairing (docs/notify-protocol.md, "Pairing over
// Wi-Fi"): POST /api/devices/pair → show the code → poll until approved,
// expired or gone. Pure state machine: the app performs the HTTP and feeds
// status + body back in; time arrives as nowMs.

enum PairPhase : uint8_t {
  PAIR_IDLE,
  PAIR_REQUESTING,   // POST /api/devices/pair due
  PAIR_WAITING,      // code on screen, polling
  PAIR_APPROVED,     // token + connect ready to store
  PAIR_EXPIRED,      // expired / 404 / 410 / local deadline
  PAIR_FAILED,       // the pair request itself failed (error says why)
};

struct PairSession {
  PairPhase phase;
  char base[100];        // http(s)://host[:port], derived from the ws url
  char shownHost[64];    // the "Enter at" host: console if given, else notify
  char pairingId[48];
  char secret[80];
  char code[10];         // XXX-XXX
  uint32_t waitStartMs;
  uint32_t ttlMs;
  uint32_t intervalMs;
  uint32_t nextPollMs;
  char token[100];
  char connect[128];     // approved connect url; starts as the configured ws url
  char error[32];
};

/** pairBaseUrl maps ws(s)://host[:port]/… to http(s)://host[:port]; false if not a ws url. */
bool pairBaseUrl(const char* wsUrl, char* base, size_t cap);

/** pairHostOf extracts the host from a URL or bare host ("https://a.b/c" → "a.b"). */
void pairHostOf(const char* url, char* out, size_t cap);

/** pairFormatCode uppercases, strips separators and shows six chars as XXX-XXX. */
void pairFormatCode(const char* in, char* out, size_t cap);

/** pairStart resets the session to REQUESTING; console may be null. False on a bad url. */
bool pairStart(PairSession& s, const char* wsUrl, const char* console);

/** pairOnCreated applies the POST /api/devices/pair reply (status <= 0 = no response). */
void pairOnCreated(PairSession& s, int status, const char* body, uint32_t nowMs);

/** pairOnPoll applies a GET /api/devices/pair/{id} reply (status <= 0 = no response). */
void pairOnPoll(PairSession& s, int status, const char* body, uint32_t nowMs);

/** pairPollDue is true when a poll should go out now. */
bool pairPollDue(const PairSession& s, uint32_t nowMs);

/** pairCheckDeadline expires a WAITING session one interval past expiresIn; true if it did. */
bool pairCheckDeadline(PairSession& s, uint32_t nowMs);

/** pairSecondsLeft is the code's remaining lifetime (0 when not waiting). */
uint32_t pairSecondsLeft(const PairSession& s, uint32_t nowMs);

/** pairWipe returns to IDLE and zeroes the secret and token. */
void pairWipe(PairSession& s);
