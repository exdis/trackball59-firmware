#include "language_shift.h"
#include "qmk_settings.h"
#include <assert.h>

qmk_settings_t QS;
unsigned macro_calls[256];
bool simulate_balanced_macros;

/* Only persistence and macro payload delivery are mocked. */
void qmk_settings_init(void) {
    QS = (qmk_settings_t){.tapping_term = 350, .quick_tap_term = 175};
}

void dynamic_keymap_macro_send(uint8_t id) {
    ++macro_calls[id];
    if (!simulate_balanced_macros) {
        return;
    }
    /* Only M0/M1 balanced basic-key shortcuts, not the Vial macro parser. */
    assert(id <= 1);
    register_code(KC_LCTL);
    register_code(KC_LSFT);
    register_code(KC_LGUI);
    if (id == 1) register_code(KC_LALT);
    register_code(KC_BSLS);
    wait_ms(50);
    unregister_code(KC_BSLS);
    if (id == 1) unregister_code(KC_LALT);
    unregister_code(KC_LGUI);
    unregister_code(KC_LSFT);
    unregister_code(KC_LCTL);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    return process_language_shift(keycode, record);
}

/* Same forwarding as vial.c without pulling in the raw-HID/EEPROM stack. */
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    return qs_get_tapping_term(keycode, record);
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record) {
    return QS_tapping_permissive_hold;
}
