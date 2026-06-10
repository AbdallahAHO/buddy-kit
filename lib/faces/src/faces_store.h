#pragma once
#include <stdint.h>

// Species persistence is the app's policy, not the faces lib's. The app
// links these two symbols (the buddy app stores NVS "buddy"/"species",
// default 0xFF = GIF character). Missing impl = loud link error.
uint8_t facesSpeciesLoad();
void    facesSpeciesSave(uint8_t idx);
