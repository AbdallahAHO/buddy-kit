// App-side impl of the faces persistence contract. Bodies match the
// original stats.h speciesIdxLoad/Save so NVS data on already-flashed
// devices keeps working (namespace "buddy", key "species", 0xFF = GIF).
#include "faces_store.h"
#include <Preferences.h>

static Preferences _facesPrefs;

uint8_t facesSpeciesLoad() {
  _facesPrefs.begin("buddy", true);
  uint8_t v = _facesPrefs.getUChar("species", 0xFF);
  _facesPrefs.end();
  return v;
}

void facesSpeciesSave(uint8_t idx) {
  _facesPrefs.begin("buddy", false);
  _facesPrefs.putUChar("species", idx);
  _facesPrefs.end();
}
