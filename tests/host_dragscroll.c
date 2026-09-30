#include <assert.h>
#include <stdio.h>

layer_state_t layer_state, default_layer_state;
static bool master;
static uint16_t cpi;
static unsigned cpi_calls, kb_writes, user_writes, user_reports, matrix_inits;
static uint32_t kb_eeprom, user_eeprom, layout_options;
static bool language_pass;
static unsigned language_calls;
static uint16_t language_code;
static keyrecord_t *language_record;

bool process_language_shift(uint16_t keycode, keyrecord_t *record) {
    ++language_calls;
    language_code = keycode;
    language_record = record;
    return language_pass;
}

bool is_keyboard_master(void) { return master; }
bool layer_state_cmp(layer_state_t state, uint8_t layer) {
    /* QMK treats an empty state as base layer 0. */
    return state ? (state & ((layer_state_t)1 << layer)) != 0 : layer == 0;
}
void pointing_device_set_cpi(uint16_t value) { cpi = value; ++cpi_calls; }
uint32_t eeconfig_read_kb(void) { return kb_eeprom; }
uint32_t eeconfig_read_user(void) { return user_eeprom; }
void eeconfig_update_kb(uint32_t value) { kb_eeprom = value; ++kb_writes; }
void eeconfig_update_user(uint32_t value) { user_eeprom = value; ++user_writes; }
uint32_t via_get_layout_options(void) { return layout_options; }
uint8_t get_mods(void) { return 0; }
uint8_t get_oneshot_mods(void) { return 0; }
uint8_t mod_config(uint8_t mods) { return mods; }
report_mouse_t pointing_device_task_user(report_mouse_t report) { ++user_reports; return report; }
void matrix_init_user(void) { ++matrix_inits; }

#define BIT(n) ((layer_state_t)1 << (n))

static void reset(void) {
    language_pass = true;
    language_calls = 0;
    master = true;
    layer_state = 0;
    default_layer_state = BIT(0);
    dragscroll_layer = 0;
    dragscroll_layer_ready = false;
    layout_options = 0;
    user_eeprom = 0x1234;
    g_charybdis_config.raw = 0;
    g_charybdis_config.pointer_default_dpi = 4; /* 1200 CPI */
    g_charybdis_config.pointer_sniping_dpi = 1; /* 300 CPI */
    g_charybdis_config.pointer_dragscroll_dpi = 100;
    kb_eeprom = g_charybdis_config.raw;
    matrix_init_kb();
    cpi = 0;
    cpi_calls = kb_writes = user_writes = user_reports = matrix_inits = 0;
}

static void ready(uint32_t option) {
    layout_options = option;
    keyboard_post_init_user();
    assert(dragscroll_layer_ready);
    assert(user_config.raw1 == user_eeprom);
}

static void key(uint16_t code, bool pressed) {
    keyrecord_t record = {.event = {.pressed = pressed}};
    assert(process_record_kb(code, &record));
}

static report_mouse_t move(int8_t x, int8_t y) {
    return pointing_device_task_kb((report_mouse_t){.x = x, .y = y, .buttons = 5});
}

static void test_startup(void) {
    reset();
    layer_state = BIT(2);
    via_set_layout_options_kb(2);
    assert(dragscroll_layer == 2);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    assert(cpi_calls == 0);
    assert(default_layer_state_set_user(BIT(2)) == BIT(2));
    assert(cpi_calls == 0);
    /* Existing sniping hook may set CPI; the new layer request must not run. */
    assert(layer_state_set_user(BIT(2)) == BIT(2));
    assert(!layer_dragscroll_enabled);
    assert(cpi == 1200);
    cpi_calls = 0;
    layout_options = 2;
    keyboard_post_init_kb();
    assert(dragscroll_layer_ready && charybdis_get_pointer_dragscroll_enabled());
    assert(cpi == 100 && cpi_calls == 2);
    assert(user_config.raw1 == user_eeprom);
    assert(kb_writes == 0 && user_writes == 0);
    reset();
    layer_state = BIT(2);
    via_set_layout_options_kb(2);
    ready(0); /* Post-init reloads persisted options, not stale hook state. */
    assert(!charybdis_get_pointer_dragscroll_enabled());
}

static void test_all_options(void) {
    for (unsigned selected = 0; selected < 16; ++selected) {
        reset();
        layer_state = 0xffff;
        via_set_layout_options_kb(selected);
        assert(!charybdis_get_pointer_dragscroll_enabled() && cpi_calls == 0);
        ready(selected);
        assert(charybdis_get_pointer_dragscroll_enabled() == (selected > 0 && selected < 10));
        assert(dragscroll_layer == (selected < 10 ? selected : 0));
        assert(kb_writes == 0 && user_writes == 0);
        if (selected > 0 && selected < 10) {
            layer_state = BIT(0);
            via_set_layout_options_kb(selected);
            assert(!charybdis_get_pointer_dragscroll_enabled());
            layer_state = BIT(selected);
            via_set_layout_options_kb(selected);
            assert(charybdis_get_pointer_dragscroll_enabled());
            via_set_layout_options_kb(0);
            assert(!charybdis_get_pointer_dragscroll_enabled());
        }
    }
    reset();
    layer_state = BIT(2);
    ready(0xfffffff2U); /* Only the selector's low nibble is consumed. */
    assert(dragscroll_layer == 2 && charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(UINT32_MAX);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(0x10);
    assert(dragscroll_layer == 0);
}

static void test_live_and_incoming_states(void) {
    reset();
    layer_state = BIT(2) | BIT(9);
    ready(2); /* Selected layer need not be highest. */
    assert(charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(4);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(9);
    assert(charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(2);
    assert(layer_state_set_user(BIT(9)) == BIT(9));
    assert(!charybdis_get_pointer_dragscroll_enabled()); /* Ignore stale global bit 2. */
    layer_state = BIT(9);
    assert(layer_state_set_user(BIT(2) | BIT(9)) == (BIT(2) | BIT(9)));
    assert(charybdis_get_pointer_dragscroll_enabled());
    assert(layer_state == BIT(9)); /* QMK, not the hook, commits returned state. */
    assert(kb_writes == 0 && user_writes == 0);
}

static void test_default_state(void) {
    reset();
    default_layer_state = BIT(2);
    ready(2);
    assert(charybdis_get_pointer_dragscroll_enabled());
    assert(layer_state_set_user(BIT(9)) == BIT(9));
    assert(charybdis_get_pointer_dragscroll_enabled());
    assert(default_layer_state_set_user(BIT(0)) == BIT(0));
    assert(!charybdis_get_pointer_dragscroll_enabled()); /* Ignore old default. */
    default_layer_state = BIT(0);
    assert(default_layer_state_set_user(BIT(2) | BIT(9)) == (BIT(2) | BIT(9)));
    assert(charybdis_get_pointer_dragscroll_enabled());
    layer_state = BIT(2);
    default_layer_state_set_user(BIT(0));
    assert(charybdis_get_pointer_dragscroll_enabled()); /* Normal layer still requests it. */
    via_set_layout_options_kb(0);
    assert(!charybdis_get_pointer_dragscroll_enabled());
}

static void test_nonmaster(void) {
    reset();
    master = false;
    layer_state = BIT(2);
    ready(2);
    via_set_layout_options_kb(2);
    default_layer_state_set_user(BIT(2));
    assert(!layer_dragscroll_enabled && cpi_calls == 0);
    layer_state_set_user(BIT(2));
    assert(!layer_dragscroll_enabled);
    /* The pre-existing sniping hook is not master-guarded. */
    assert(cpi_calls == 1 && cpi == 1200);
    charybdis_set_pointer_dragscroll_enabled(true);
    report_mouse_t report = move(4, -3);
    assert(report.x == 4 && report.y == -3 && report.h == 0 && report.v == 0);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0 && user_reports == 0);
    master = true;
    via_set_layout_options_kb(2);
    master = false;
    via_set_layout_options_kb(0);
    assert(layer_dragscroll_enabled); /* Nonmaster hooks must not change either direction. */
}

static void test_manual_keys(void) {
    reset();
    layer_state = BIT(2);
    ready(2);
    key(DRGSCRL, true);
    assert(manual_dragscroll_enabled);
    key(DRGSCRL, false);
    assert(!manual_dragscroll_enabled && charybdis_get_pointer_dragscroll_enabled());
    key(DRG_TOG, false);
    assert(!manual_dragscroll_enabled);
    key(DRG_TOG, true); /* Must toggle manual, not effective state (already on). */
    assert(manual_dragscroll_enabled);
    key(DRG_TOG, false);
    via_set_layout_options_kb(0);
    assert(charybdis_get_pointer_dragscroll_enabled());
    key(DRG_TOG, true);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(2);
    key(DRG_TOG, true);
    key(DRG_TOG, true);
    assert(!manual_dragscroll_enabled && charybdis_get_pointer_dragscroll_enabled());
    via_set_layout_options_kb(0);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    key(DRGSCRL, true);
    via_set_layout_options_kb(2);
    via_set_layout_options_kb(0);
    assert(charybdis_get_pointer_dragscroll_enabled());
    key(DRGSCRL, false);
    assert(!charybdis_get_pointer_dragscroll_enabled());
    assert(kb_writes == 0 && user_writes == 0);
}

static void test_buffers_and_or_transitions(void) {
    reset();
    charybdis_set_pointer_dragscroll_enabled(true);
    unsigned calls = cpi_calls;
    report_mouse_t report = move(4, 3);
    assert(report.x == 0 && report.y == 0 && report.h == 0 && report.v == 0 && report.buttons == 5);
    assert(scroll_buffer_x == 4 && scroll_buffer_y == -3); /* Real config reverses Y. */
    charybdis_set_pointer_dragscroll_enabled(true);
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    charybdis_set_pointer_dragscroll_enabled(false);
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    assert(cpi_calls == calls && scroll_buffer_x == 4 && scroll_buffer_y == -3);
    report = move(2, 3);
    assert(report.h == 0 && report.v == 0); /* Exactly six is still partial. */
    report = move(1, 1);
    assert(report.h == 1 && report.v == -1);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0);
    move(-4, -3);
    charybdis_set_pointer_layer_dragscroll_enabled(false);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0 && cpi_calls == calls + 1);
    report = move(3, -2);
    assert(report.x == 3 && report.y == -2 && report.h == 0 && report.v == 0);
    /* Seed both buffers to verify the enabling transition also clears them. */
    scroll_buffer_x = 5;
    scroll_buffer_y = -5;
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0);
    report = move(-3, -4);
    assert(report.h == 0 && report.v == 0);
    charybdis_set_pointer_dragscroll_enabled(true);
    charybdis_set_pointer_layer_dragscroll_enabled(false);
    assert(scroll_buffer_x == -3 && scroll_buffer_y == 4);
    report = move(-4, -3);
    assert(report.h == -1 && report.v == 1);
    move(2, 2);
    charybdis_set_pointer_dragscroll_enabled(false);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0);
    calls = cpi_calls;
    charybdis_set_pointer_dragscroll_enabled(false);
    charybdis_set_pointer_layer_dragscroll_enabled(false);
    assert(cpi_calls == calls);
}

static void test_live_hooks_preserve_buffers(void) {
    reset();
    layer_state = BIT(2) | BIT(4);
    ready(2);
    move(4, 3);
    via_set_layout_options_kb(4); /* Switch between two active layers. */
    assert(scroll_buffer_x == 4 && scroll_buffer_y == -3);
    layer_state_set_user(layer_state | BIT(9));
    default_layer_state_set_user(BIT(0));
    key(DRGSCRL, true);
    key(DRGSCRL, false);
    key(DRG_TOG, true);
    key(DRG_TOG, true);
    assert(scroll_buffer_x == 4 && scroll_buffer_y == -3);
    via_set_layout_options_kb(5);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0);
    via_set_layout_options_kb(2);
    report_mouse_t report = move(3, 4);
    assert(report.h == 0 && report.v == 0);
}

static void test_cpi_priority_and_sniping(void) {
    reset();
    keyboard_post_init_kb();
    assert(cpi == 1200);
    key(SNIPING, true);
    assert(cpi == 300);
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    assert(cpi == 100);
    charybdis_cycle_pointer_default_dpi_noeeprom(true);
    charybdis_cycle_pointer_sniping_dpi_noeeprom(true);
    assert(cpi == 100);
    charybdis_set_pointer_dragscroll_enabled(true);
    charybdis_set_pointer_layer_dragscroll_enabled(false);
    assert(cpi == 100);
    charybdis_set_pointer_dragscroll_enabled(false);
    assert(cpi == 400);
    key(SNIPING, false);
    assert(cpi == 1400);
    layer_state = 0;
    via_set_layout_options_kb(3);
    layer_state_set_user(BIT(3));
    assert(charybdis_get_pointer_sniping_enabled() && cpi == 100);
    via_set_layout_options_kb(0);
    assert(charybdis_get_pointer_sniping_enabled() && cpi == 400);
    layer_state_set_user(0);
    assert(!charybdis_get_pointer_sniping_enabled() && cpi == 1400);
    assert(kb_writes == 0 && user_writes == 0);
}

static void test_initialization_resets(void) {
    reset();
    charybdis_set_pointer_dragscroll_enabled(true);
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    charybdis_set_pointer_sniping_enabled(true);
    move(4, 3);
    kb_eeprom = g_charybdis_config.raw; /* Even persisted mode bits are cleared on read. */
    matrix_init_kb();
    assert(!manual_dragscroll_enabled && !layer_dragscroll_enabled);
    assert(!charybdis_get_pointer_dragscroll_enabled() && !charybdis_get_pointer_sniping_enabled());
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0 && matrix_inits == 1);
    assert(charybdis_get_pointer_default_dpi() == 1200 && kb_writes == 0);
    charybdis_set_pointer_layer_dragscroll_enabled(true);
    move(4, 3);
    eeconfig_init_kb();
    assert(!manual_dragscroll_enabled && !layer_dragscroll_enabled && g_charybdis_config.raw == 0);
    assert(scroll_buffer_x == 0 && scroll_buffer_y == 0);
    assert(cpi == 400 && kb_writes == 1 && user_writes == 1);
}

static void test_language_dispatch_gate(void) {
    reset();
    keyrecord_t record = {.event = {.pressed = true}};
    language_pass = false;
    uint32_t before = user_config.raw1;
    assert(!process_record_user(AUTO_MODE_TOGGLE, &record));
    assert(user_config.raw1 == before);
    assert(language_calls == 1 && language_code == AUTO_MODE_TOGGLE);
    assert(language_record == &record);
    record.event.pressed = false;
    assert(!process_record_user(4, &record));
    assert(language_calls == 2 && language_code == 4 && language_record == &record);
    language_pass = true;
    assert(process_record_user(4, &record));
    record.event.pressed = true;
    assert(!process_record_user(AUTO_MODE_TOGGLE, &record));
    assert(user_config.raw1 != before);
    assert(language_calls == 4);
}

int main(void) {
#define RUN(test) do { test(); puts("PASS " #test); } while (0)
    RUN(test_startup);
    RUN(test_all_options);
    RUN(test_live_and_incoming_states);
    RUN(test_default_state);
    RUN(test_nonmaster);
    RUN(test_manual_keys);
    RUN(test_buffers_and_or_transitions);
    RUN(test_live_hooks_preserve_buffers);
    RUN(test_cpi_priority_and_sniping);
    RUN(test_initialization_resets);
    RUN(test_language_dispatch_gate);
    return 0;
}
