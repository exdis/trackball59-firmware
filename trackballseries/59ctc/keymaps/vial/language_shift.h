#pragma once

#include "quantum.h"

// Reserve complete Mod-Tap codes, not the standalone F23/F24 keys.
#define LANG_LSFT LSFT_T(KC_F23)
#define LANG_RSFT RSFT_T(KC_F24)

bool process_language_shift(uint16_t keycode, keyrecord_t *record);
