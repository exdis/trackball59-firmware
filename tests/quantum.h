#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint32_t layer_state_t;
typedef struct { struct { bool pressed; } event; } keyrecord_t;
typedef struct { int8_t x, y, h, v; uint8_t buttons; } report_mouse_t;

#define QK_KB_0 0x7e00
#define MOD_MASK_SHIFT 0x22
#define IS_QK_KB(code) ((code) >= QK_KB_0 && (code) < QK_KB_0 + 32)
#define IS_MOUSEKEY(code) false

extern layer_state_t layer_state, default_layer_state;
bool is_keyboard_master(void);
bool layer_state_cmp(layer_state_t state, uint8_t layer);
void pointing_device_set_cpi(uint16_t cpi);
uint32_t eeconfig_read_kb(void);
uint32_t eeconfig_read_user(void);
void eeconfig_update_kb(uint32_t value);
void eeconfig_update_user(uint32_t value);
uint32_t via_get_layout_options(void);
uint8_t get_mods(void);
uint8_t get_oneshot_mods(void);
uint8_t mod_config(uint8_t mods);
bool process_record_user(uint16_t keycode, keyrecord_t *record);
bool process_language_shift(uint16_t keycode, keyrecord_t *record);
report_mouse_t pointing_device_task_user(report_mouse_t report);
void matrix_init_user(void);
void eeconfig_init_user(void);
void keyboard_post_init_user(void);
