#include "language_shift.h"
#include "dynamic_keymap.h"
#include "qmk_settings.h"

static bool is_language_shift(uint16_t keycode) {
    return keycode == LANG_LSFT || keycode == LANG_RSFT;
}

uint16_t qs_get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    return is_language_shift(keycode) ? 200 : QS.tapping_term;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    return is_language_shift(keycode) || QS_tapping_hold_on_other_key_press;
}

uint16_t get_quick_tap_term(uint16_t keycode, keyrecord_t *record) {
    return is_language_shift(keycode) ? 0 : QS.quick_tap_term;
}

bool get_retro_tapping(uint16_t keycode, keyrecord_t *record) {
    // Retro tapping bypasses process_record_user and would leak F23/F24.
    return !is_language_shift(keycode) && QS_tapping_retro_tapping;
}

bool get_chordal_hold(uint16_t keycode, keyrecord_t *record,
                     uint16_t other_keycode, keyrecord_t *other_record) {
    if (is_language_shift(keycode)) {
        return true;
    }
    return !QS_tapping_chordal_hold || get_chordal_hold_default(record, other_record);
}

bool process_language_shift(uint16_t keycode, keyrecord_t *record) {
    if (!is_language_shift(keycode)) {
        return true;
    }
#ifdef CAPS_WORD_ENABLE
    // Match the old Tap Dance: a language/Shift press ends Caps Word.
    if (record->event.pressed) {
        caps_word_off();
    }
#endif
    if (record->tap.count > 0) {
        if (record->event.pressed) {
            // M0/M1 are balanced basic-key chords. Preserve physically held
            // modifiers that their explicit key-up actions would otherwise clear.
            uint8_t mods = get_mods();
            dynamic_keymap_macro_send(keycode == LANG_LSFT ? 0 : 1);
            set_mods(mods);
            send_keyboard_report();
        }
        // Consume both tap edges; holds continue to QMK's real Shift action.
        return false;
    }
    return true;
}
