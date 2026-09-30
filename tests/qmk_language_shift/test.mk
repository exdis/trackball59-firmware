# Run with builddefs/build_test.mk; no upstream tracked files need changing.
ifndef TRACKBALL_ROOT
$(error TRACKBALL_ROOT must name the trackball59-firmware checkout)
endif
VPATH += tests/test_common $(TRACKBALL_ROOT)/trackballseries/59ctc/keymaps/vial
SRC += language_shift_under_test.c settings_bridge.c
INTROSPECTION_KEYMAP_C = test_keymap.c
CAPS_WORD_ENABLE = yes
