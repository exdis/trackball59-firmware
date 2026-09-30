#include "test_common.hpp"
#include <cstring>
#include <algorithm>
#include <tuple>
#include <vector>

extern "C" {
#include "language_shift.h"
#include "qmk_settings.h"
extern unsigned macro_calls[256];
extern bool simulate_balanced_macros;
}

using testing::InSequence;
using testing::_;

class LanguageShift : public TestFixture {
protected:
    void SetUp() override {
        qmk_settings_init();
        caps_word_off();
        simulate_balanced_macros = false;
        std::memset(macro_calls, 0, sizeof(macro_calls));
    }

    void expect_macros(unsigned left, unsigned right) {
        EXPECT_EQ(macro_calls[0], left);
        EXPECT_EQ(macro_calls[1], right);
        for (unsigned id = 2; id < 256; ++id) EXPECT_EQ(macro_calls[id], 0u);
    }
};

TEST_F(LanguageShift, CompleteKeycodesAndExactSettingsFallback) {
    EXPECT_EQ(LANG_LSFT, LSFT_T(KC_F23));
    EXPECT_EQ(LANG_RSFT, RSFT_T(KC_F24));
    keyrecord_t record = {}, other = {};
    record.event.key = {1, 0};
    other.event.key = {2, 0};
    std::memset(&QS, 0x5a, sizeof(QS));
    const uint16_t terms[] = {0, 1, 321, 65535};
    for (unsigned bits = 0; bits < 16; ++bits) {
        QS.tapping_v2 = bits;
        QS.tapping_term = terms[bits % 4];
        QS.quick_tap_term = terms[(bits + 1) % 4];
        const auto before = QS;
        for (uint32_t code = 0; code <= UINT16_MAX; ++code) {
            const bool language = code == LANG_LSFT || code == LANG_RSFT;
            ASSERT_EQ(qs_get_tapping_term(code, &record), language ? 200 : QS.tapping_term) << code;
            ASSERT_EQ(get_quick_tap_term(code, &record), language ? 0 : QS.quick_tap_term) << code;
            ASSERT_EQ(get_hold_on_other_key_press(code, &record), language || bool(bits & 2)) << code;
            ASSERT_EQ(get_retro_tapping(code, &record), !language && bool(bits & 4)) << code;
            for (uint8_t col : {uint8_t(2), uint8_t(8)}) {
                other.event.key.col = col;
                ASSERT_EQ(get_chordal_hold(code, &record, KC_A, &other),
                          language || !(bits & 8) || get_chordal_hold_default(&record, &other)) << code;
            }
        }
        EXPECT_EQ(std::memcmp(&QS, &before, sizeof(QS)), 0);
    }
    expect_macros(0, 0);
}

TEST_F(LanguageShift, DispatchConsumesOnlyTapEdgesAndNeverChangesSettings) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    keyrecord_t record = {};
    const auto before = QS;
    unsigned expected[2] = {};
    for (uint32_t code = 0; code <= UINT16_MAX; ++code) {
        for (unsigned count : {0u, 1u, 2u, 15u}) {
            record.tap.count = count;
            for (bool pressed : {true, false}) {
                record.event.pressed = pressed;
                const bool tap = count && (code == LANG_LSFT || code == LANG_RSFT);
                ASSERT_EQ(process_language_shift(code, &record), !tap) << code;
                if (tap && pressed) ++expected[code == LANG_RSFT];
                ASSERT_EQ(macro_calls[0], expected[0]) << code;
                ASSERT_EQ(macro_calls[1], expected[1]) << code;
                ASSERT_EQ(record.tap.count, count);
                ASSERT_EQ(record.event.pressed, pressed);
            }
        }
    }
    expect_macros(3, 3);
    EXPECT_EQ(std::memcmp(&QS, &before, sizeof(QS)), 0);
    VERIFY_AND_CLEAR(driver);
}

TEST_F(LanguageShift, CapsWordStopsOnlyOnReservedPressNotReleaseOrOtherKey) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    keyrecord_t record = {};
    const uint16_t codes[] = {LANG_LSFT, LANG_RSFT, KC_F23, KC_F24, LCTL_T(KC_S)};
    for (uint16_t code : codes) {
        for (unsigned count : {0u, 1u}) {
            record.tap.count = count;
            caps_word_on();
            ASSERT_TRUE(is_caps_word_on());
            record.event.pressed = false;
            process_language_shift(code, &record);
            EXPECT_TRUE(is_caps_word_on());
            record.event.pressed = true;
            process_language_shift(code, &record);
            EXPECT_EQ(is_caps_word_on(), code != LANG_LSFT && code != LANG_RSFT);
            caps_word_off();
        }
    }
    expect_macros(1, 1);
    VERIFY_AND_CLEAR(driver);
}

class LanguageShiftSequence : public LanguageShift, public testing::WithParamInterface<uint16_t> {};
INSTANTIATE_TEST_CASE_P(BothShifts, LanguageShiftSequence, testing::Values(LANG_LSFT, LANG_RSFT));

TEST_P(LanguageShiftSequence, TapAloneSendsMacroOnceWithoutPlaceholderReports) {
    TestDriver driver;
    auto shift = KeymapKey(0, 1, 0, GetParam());
    set_keymap({shift});
    caps_word_on();
    ASSERT_TRUE(is_caps_word_on());
    EXPECT_NO_REPORT(driver);
    tap_key(shift, 199);
    EXPECT_FALSE(is_caps_word_on());
    idle_for(400);
    VERIFY_AND_CLEAR(driver);
    expect_macros(GetParam() == LANG_LSFT, GetParam() == LANG_RSFT);
}

TEST_P(LanguageShiftSequence, RapidSameHandCapitalIgnoresGlobalChordalAndRetro) {
    TestDriver driver;
    InSequence sequence;
    QS.tapping_v2 = 12; // Global chordal + retro on, global hold-on-press off.
    auto shift = KeymapKey(0, 1, 0, GetParam());
    auto letter = KeymapKey(0, 2, 0, KC_A);
    const auto mod = GetParam() == LANG_LSFT ? KC_LSFT : KC_RSFT;
    set_keymap({shift, letter});
    caps_word_on();
    ASSERT_TRUE(is_caps_word_on());
    EXPECT_NO_REPORT(driver);
    shift.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (mod));
    EXPECT_REPORT(driver, (mod, KC_A));
    letter.press();
    run_one_scan_loop();
    EXPECT_FALSE(is_caps_word_on());
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_A));
    shift.release();
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    letter.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_P(LanguageShiftSequence, TimeoutUses200msNotGlobalAndDoesNotRetroTap) {
    TestDriver driver;
    InSequence sequence;
    QS.tapping_v2 = 12;
    auto shift = KeymapKey(0, 1, 0, GetParam());
    const auto mod = GetParam() == LANG_LSFT ? KC_LSFT : KC_RSFT;
    set_keymap({shift});
    caps_word_on();
    ASSERT_TRUE(is_caps_word_on());
    EXPECT_NO_REPORT(driver);
    shift.press();
    idle_for(200);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (mod));
    run_one_scan_loop();
    EXPECT_FALSE(is_caps_word_on());
    VERIFY_AND_CLEAR(driver);
    EXPECT_EMPTY_REPORT(driver);
    shift.release();
    run_one_scan_loop();
    idle_for(400);
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_P(LanguageShiftSequence, TapThenQuickSecondPressCanHold) {
    TestDriver driver;
    InSequence sequence;
    auto shift = KeymapKey(0, 1, 0, GetParam());
    auto letter = KeymapKey(0, 2, 0, KC_A);
    const auto mod = GetParam() == LANG_LSFT ? KC_LSFT : KC_RSFT;
    set_keymap({shift, letter});
    EXPECT_NO_REPORT(driver);
    tap_key(shift, 10);
    idle_for(10);
    shift.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    expect_macros(GetParam() == LANG_LSFT, GetParam() == LANG_RSFT);
    EXPECT_REPORT(driver, (mod));
    EXPECT_REPORT(driver, (mod, KC_A));
    EXPECT_REPORT(driver, (mod));
    tap_key(letter);
    EXPECT_EMPTY_REPORT(driver);
    shift.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(GetParam() == LANG_LSFT, GetParam() == LANG_RSFT);
}

TEST_F(LanguageShift, BothShiftsHeldAndReleasedIndependently) {
    TestDriver driver;
    InSequence sequence;
    auto left = KeymapKey(0, 1, 0, LANG_LSFT);
    auto right = KeymapKey(0, 8, 0, LANG_RSFT);
    auto letter = KeymapKey(0, 2, 0, KC_A);
    set_keymap({left, right, letter});
    EXPECT_NO_REPORT(driver);
    left.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_LSFT));
    right.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_LSFT, KC_RSFT));
    EXPECT_REPORT(driver, (KC_LSFT, KC_RSFT, KC_A));
    EXPECT_REPORT(driver, (KC_LSFT, KC_RSFT));
    tap_key(letter);
    EXPECT_REPORT(driver, (KC_RSFT));
    left.release();
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    right.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, HomeRowSameHandChordStillTaps) {
    TestDriver driver;
    InSequence sequence;
    QS.tapping_v2 = 10; // Chordal hold still vetoes global hold-on-press for other keys.
    auto home = KeymapKey(0, 1, 0, LCTL_T(KC_S));
    auto letter = KeymapKey(0, 2, 0, KC_A);
    set_keymap({home, letter});
    EXPECT_NO_REPORT(driver);
    home.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_S));
    EXPECT_REPORT(driver, (KC_S, KC_A));
    letter.press();
    run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_A));
    home.release();
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    letter.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, HomeRowRetainsGlobalTermAndRetroTap) {
    TestDriver driver;
    InSequence sequence;
    QS.tapping_v2 = 4;
    auto home = KeymapKey(0, 1, 0, LCTL_T(KC_S));
    set_keymap({home});
    EXPECT_NO_REPORT(driver);
    home.press();
    idle_for(350);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_LCTL));
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    EXPECT_REPORT(driver, (KC_S));
    EXPECT_EMPTY_REPORT(driver);
    home.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, StandaloneF23AndF24AreNotReserved) {
    TestDriver driver;
    InSequence sequence;
    auto f23 = KeymapKey(0, 1, 0, KC_F23);
    auto f24 = KeymapKey(0, 2, 0, KC_F24);
    set_keymap({f23, f24});
    EXPECT_REPORT(driver, (KC_F23));
    EXPECT_EMPTY_REPORT(driver);
    tap_key(f23);
    EXPECT_REPORT(driver, (KC_F24));
    EXPECT_EMPTY_REPORT(driver);
    tap_key(f24);
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, HomeRowQuickSecondPressStillRepeatsTap) {
    TestDriver driver;
    InSequence sequence;
    auto home = KeymapKey(0, 1, 0, LCTL_T(KC_S));
    set_keymap({home});
    EXPECT_REPORT(driver, (KC_S));
    EXPECT_EMPTY_REPORT(driver);
    tap_key(home, 10);
    idle_for(10);
    EXPECT_REPORT(driver, (KC_S));
    home.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    EXPECT_NO_REPORT(driver);
    idle_for(400); // Even beyond the global tapping term, quick-tap stays tapped.
    VERIFY_AND_CLEAR(driver);
    EXPECT_EMPTY_REPORT(driver);
    home.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, HomeRowGlobalHoldOnPressStillWorksWithChordalDisabled) {
    TestDriver driver;
    InSequence sequence;
    QS.tapping_v2 = 2;
    auto home = KeymapKey(0, 1, 0, LCTL_T(KC_S));
    auto letter = KeymapKey(0, 2, 0, KC_A);
    set_keymap({home, letter});
    EXPECT_NO_REPORT(driver);
    home.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_LCTL));
    EXPECT_REPORT(driver, (KC_LCTL, KC_A));
    EXPECT_REPORT(driver, (KC_LCTL));
    tap_key(letter);
    EXPECT_EMPTY_REPORT(driver);
    home.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

TEST_F(LanguageShift, HomeRowDoesNotInheritLanguageHoldOnPress) {
    TestDriver driver;
    InSequence sequence;
    auto home = KeymapKey(0, 1, 0, LCTL_T(KC_S));
    auto letter = KeymapKey(0, 2, 0, KC_A);
    set_keymap({home, letter});
    EXPECT_NO_REPORT(driver);
    home.press();
    idle_for(10);
    letter.press();
    idle_for(10);
    VERIFY_AND_CLEAR(driver);
    EXPECT_REPORT(driver, (KC_S));
    EXPECT_REPORT(driver, (KC_S, KC_A));
    EXPECT_REPORT(driver, (KC_A));
    home.release();
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    letter.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
    expect_macros(0, 0);
}

class BalancedLanguageMacro : public LanguageShift,
                              public testing::WithParamInterface<std::tuple<uint16_t, uint8_t>> {};
INSTANTIATE_TEST_CASE_P(M0M1HeldModifiers, BalancedLanguageMacro,
    testing::Combine(testing::Values(uint16_t(LANG_LSFT), uint16_t(LANG_RSFT)),
                     testing::Values(uint8_t(0), uint8_t(MOD_LCTL), uint8_t(MOD_LSFT),
                                     uint8_t(MOD_LCTL | MOD_LSFT), uint8_t(MOD_BIT(KC_RCTL) | MOD_BIT(KC_RSFT)))));

TEST_P(BalancedLanguageMacro, BalancedBasicShortcutRestoresPhysicalModsAndHostReport) {
    TestDriver driver;
    const auto code = std::get<0>(GetParam());
    const uint8_t held = std::get<1>(GetParam());
    const bool right = held & MOD_BIT(KC_RCTL);
    const uint16_t ctrl_code = right ? KC_RCTL : KC_LCTL;
    const uint16_t shift_code = right ? KC_RSFT : KC_LSFT;
    const uint8_t ctrl_mod = MOD_BIT(ctrl_code), shift_mod = MOD_BIT(shift_code);
    auto ctrl = KeymapKey(0, 0, 0, ctrl_code);
    auto shift = KeymapKey(0, 1, 0, shift_code);
    auto language = KeymapKey(0, 2, 0, code);
    auto letter = KeymapKey(0, 3, 0, KC_A);
    set_keymap({ctrl, shift, language, letter});
    if (held & ctrl_mod) {
        EXPECT_REPORT(driver, (ctrl_code));
        ctrl.press();
        run_one_scan_loop();
        VERIFY_AND_CLEAR(driver);
    }
    if (held & shift_mod) {
        if (held & ctrl_mod) {
            EXPECT_REPORT(driver, (ctrl_code, shift_code));
        } else {
            EXPECT_REPORT(driver, (shift_code));
        }
        shift.press();
        run_one_scan_loop();
        VERIFY_AND_CLEAR(driver);
    }
    ASSERT_EQ(get_mods(), held);
    simulate_balanced_macros = true;

    // Capture every host report, including transient releases before restoration.
    using State = std::pair<uint8_t, uint8_t>;
    std::vector<State> reports;
    std::vector<uint16_t> times;
    EXPECT_CALL(driver, send_keyboard_mock(_)).Times(testing::AtLeast(1))
        .WillRepeatedly([&](report_keyboard_t &report) {
            reports.emplace_back(report.mods, report.keys[0]);
            times.push_back(timer_read());
            for (unsigned i = 1; i < sizeof(report.keys); ++i) EXPECT_EQ(report.keys[i], 0);
        });
    tap_key(language, 10);
    VERIFY_AND_CLEAR(driver);

    const uint8_t chord = MOD_LCTL | MOD_LSFT | MOD_LGUI;
    const uint8_t alt = code == LANG_RSFT ? MOD_LALT : 0;
    std::vector<State> expected = {
        {held, 0}, // Initial report is already sent; QMK suppresses duplicates.
        {uint8_t(held | MOD_LCTL), 0},
        {uint8_t(held | MOD_LCTL | MOD_LSFT), 0},
        {uint8_t(held | chord), 0},
        {uint8_t(held | chord | alt), 0},
        {uint8_t(held | chord | alt), KC_BSLS},
        {uint8_t(held | chord | alt), 0},
        {uint8_t(held | chord), 0},
        {uint8_t((held | chord) & ~MOD_LGUI), 0},
        {uint8_t((held | MOD_LCTL) & ~MOD_LSFT), 0},
        {uint8_t(held & ~(MOD_LCTL | MOD_LSFT)), 0},
        {held, 0},
    };
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    expected.erase(expected.begin());
    EXPECT_EQ(reports, expected);
    const auto down = std::find(reports.begin(), reports.end(), State{uint8_t(held | chord | alt), KC_BSLS});
    ASSERT_NE(down, reports.end());
    const auto index = down - reports.begin();
    ASSERT_LT(index + 1, times.size());
    EXPECT_EQ(uint16_t(times[index + 1] - times[index]), 50);
    EXPECT_EQ(get_mods(), held);
    EXPECT_EQ(get_weak_mods(), 0);
    expect_macros(code == LANG_LSFT, code == LANG_RSFT);

    // Restored modifiers must affect the next key and release normally later.
    {
        InSequence sequence;
        if ((held & ctrl_mod) && (held & shift_mod)) {
            EXPECT_REPORT(driver, (ctrl_code, shift_code, KC_A));
            EXPECT_REPORT(driver, (ctrl_code, shift_code));
        } else if (held & ctrl_mod) {
            EXPECT_REPORT(driver, (ctrl_code, KC_A));
            EXPECT_REPORT(driver, (ctrl_code));
        } else if (held & shift_mod) {
            EXPECT_REPORT(driver, (shift_code, KC_A));
            EXPECT_REPORT(driver, (shift_code));
        } else {
            EXPECT_REPORT(driver, (KC_A));
            EXPECT_EMPTY_REPORT(driver);
        }
        tap_key(letter);
    }
    VERIFY_AND_CLEAR(driver);
    if (held & ctrl_mod) {
        if (held & shift_mod) {
            EXPECT_REPORT(driver, (shift_code));
        } else {
            EXPECT_EMPTY_REPORT(driver);
        }
        ctrl.release();
        run_one_scan_loop();
        VERIFY_AND_CLEAR(driver);
    }
    if (held & shift_mod) {
        EXPECT_EMPTY_REPORT(driver);
        shift.release();
        run_one_scan_loop();
        VERIFY_AND_CLEAR(driver);
    }
    EXPECT_EQ(get_mods(), 0);
    EXPECT_NO_REPORT(driver);
    idle_for(400);
    VERIFY_AND_CLEAR(driver);
    expect_macros(code == LANG_LSFT, code == LANG_RSFT);
}
