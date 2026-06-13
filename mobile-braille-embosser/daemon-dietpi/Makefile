CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2
CPPFLAGS += -I../shared -Isrc -MMD -MP
LDFLAGS ?=

# BRAILLATRON_LIBLOUIS=1 links liblouis for braille input/emboss translation.
# BRAILLATRON_A11Y=1 also links Speech Dispatcher, BRLTTY, and Vosk (implies liblouis).
LOUIS_LIBS :=
LOUIS_CXXFLAGS :=
ifeq ($(BRAILLATRON_LIBLOUIS),1)
LOUIS_CXXFLAGS += -DBRAILLATRON_LIBLOUIS
LOUIS_LIBS += -llouis
endif

A11Y_LIBS :=
A11Y_CXXFLAGS :=
ifeq ($(BRAILLATRON_A11Y),1)
A11Y_CXXFLAGS += -DBRAILLATRON_A11Y -DBRAILLATRON_LIBLOUIS
A11Y_LIBS += -lspeechd -lbrlapi -lvosk
LOUIS_CXXFLAGS += -DBRAILLATRON_LIBLOUIS
LOUIS_LIBS += -llouis
endif

BACKEND_CXXFLAGS := $(LOUIS_CXXFLAGS) $(A11Y_CXXFLAGS)
BACKEND_LIBS := $(LOUIS_LIBS) $(A11Y_LIBS)

DISPLAY_LIBS :=
DISPLAY_CXXFLAGS :=
ifeq ($(BRAILLATRON_DISPLAY),1)
DISPLAY_CXXFLAGS += -DBRAILLATRON_DISPLAY
DISPLAY_LIBS += -lncurses
endif
ifeq ($(shell pkg-config --exists libgpiod 2>/dev/null && echo yes),yes)
DISPLAY_CXXFLAGS += -DBRAILLATRON_GPIOD
DISPLAY_LIBS += -lgpiod
GPIOD_MAJOR := $(shell pkg-config --modversion libgpiod 2>/dev/null | cut -d. -f1)
ifneq ($(GPIOD_MAJOR),)
ifeq ($(shell test "$(GPIOD_MAJOR)" -ge 2 2>/dev/null && echo yes),yes)
DISPLAY_CXXFLAGS += -DBRAILLATRON_GPIOD_V2
endif
endif
endif
BACKEND_LIBS += $(DISPLAY_LIBS)
BACKEND_CXXFLAGS += $(DISPLAY_CXXFLAGS)

DISPLAY_OBJS := \
	src/ui/display/ui_chrome_model.o \
	src/ui/display/chrome_renderer.o \
	src/ui/display/chrome_rasterizer.o \
	src/ui/display/display_config.o \
	src/ui/display/display_backend.o \
	src/ui/display/display_composite.o \
	src/ui/display/display_fbdev.o \
	src/ui/display/font8x8_basic.o \
	src/ui/display/display_st7789.o \
	src/ui/display/display_ncurses.o

PLATFORM_OBJS := \
	src/platform/device_status.o \
	src/platform/serial_link.o \
	src/platform/shell_util.o \
	src/platform/audio_output.o

NET_OBJS := \
	src/net/wikipedia_client.o

KINEMATICS_OBJS := \
	src/kinematics/emboss_scheduler.o \
	src/kinematics/kinematics_config.o \
	src/kinematics/linkage_model.o \
	src/kinematics/motion_controller.o \
	src/kinematics/spatial_delay_line.o \
	src/kinematics/travel_log.o

DOCUMENT_OBJS := \
	src/documents/brf_store.o \
	src/documents/coordinate_state.o \
	src/documents/edit_session.o \
	src/documents/liblouis_bridge.o \
	src/documents/paper_separator.o

MOTION_OBJS := \
	src/motion/motion_service.o

HAPTICS_OBJS := \
	src/haptics/morse_encoder.o

APP_OBJS := \
	src/ui/apps/app_registry.o \
	src/ui/apps/brailler_app.o \
	src/ui/apps/calculator_app.o \
	src/ui/apps/transcriber_app.o \
	src/ui/apps/morse_learn_app.o \
	src/ui/apps/network_app.o \
	src/ui/apps/bluetooth_setup_app.o \
	src/ui/apps/library_app.o \
	src/ui/apps/localsend_app.o \
	src/ui/apps/quick_status_inline.o \
	src/ui/apps/morse_output_inline.o \
	src/ui/apps/paper_nav_inline.o \
	src/ui/apps/save_exit_inline.o \
	src/ui/apps/wikipedia_app.o \
	src/ui/apps/youtube_app.o \
	src/ui/apps/messages_app.o

CONNECT_COMMON_OBJS := \
	src/connect/connect_config.o \
	src/connect/connect_defaults.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/socket_server.o \
	src/connect/mpv_ipc.o \
	src/connect/youtube_backend.o \
	src/connect/signal_backend.o \
	src/connect/connect_client.o

CONNECTD_OBJS := \
	$(CONNECT_COMMON_OBJS) \
	src/connect/connect_service.o \
	src/connect/connect_main.o

UI_OBJS := \
	$(PLATFORM_OBJS) \
	$(NET_OBJS) \
	$(KINEMATICS_OBJS) \
	$(DOCUMENT_OBJS) \
	$(MOTION_OBJS) \
	$(HAPTICS_OBJS) \
	$(APP_OBJS) \
	../shared/protocol.o \
	src/motion_gate.o \
	src/hardware/hardware_config.o \
	src/telemetry/drv2605l.o \
	src/telemetry/i2c_device.o \
	src/telemetry/telemetry_config.o \
	src/telemetry/system_shutdown.o \
	src/telemetry/telemetry_bridge.o \
	src/ui/ui_config.o \
	$(DISPLAY_OBJS) \
	src/ui/backends/backend.o \
	src/ui/menu_overlay.o \
	src/ui/output_hub.o \
	src/ui/ui_app.o \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_defaults.o \
	src/connect/connect_config.o \
	src/ui_main.o \
	src/keyboard/chord_engine.o \
	src/keyboard/evdev_input.o \
	src/keyboard/evdev_keymap.o \
	src/keyboard/focus_nav.o \
	src/keyboard/global_hooks.o \
	src/keyboard/host_chord_assembler.o \
	src/keyboard/key_debouncer.o \
	src/keyboard/keyboard_config.o \
	src/keyboard/keyboard_service.o \
	src/keyboard/logical_keys.o \
	src/keyboard/matrix_map.o \
	src/keyboard/serial_listener.o

UI_TEST_OBJS := \
	$(PLATFORM_OBJS) \
	$(NET_OBJS) \
	$(KINEMATICS_OBJS) \
	$(DOCUMENT_OBJS) \
	$(MOTION_OBJS) \
	$(HAPTICS_OBJS) \
	$(APP_OBJS) \
	../shared/protocol.o \
	src/motion_gate.o \
	src/hardware/hardware_config.o \
	src/telemetry/drv2605l.o \
	src/telemetry/i2c_device.o \
	src/telemetry/telemetry_config.o \
	src/telemetry/system_shutdown.o \
	src/telemetry/telemetry_bridge.o \
	src/ui/ui_config.o \
	$(DISPLAY_OBJS) \
	src/ui/backends/backend.o \
	src/ui/menu_overlay.o \
	src/ui/output_hub.o \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/keyboard/focus_nav.o \
	src/ui_self_test.o

MOTION_TEST_OBJS := $(KINEMATICS_OBJS) src/motion_gate.o src/motion_self_test.o

MOTION_GATE_SYNC_TEST_OBJS := \
	src/motion_gate.o \
	src/telemetry/telemetry_bridge.o \
	src/motion_gate_sync_self_test.o

HOST_CHORD_TEST_OBJS := \
	src/keyboard/host_chord_assembler.o \
	src/keyboard/host_chord_self_test.o

WIKIPEDIA_TEST_OBJS := \
	src/platform/shell_util.o \
	src/net/wikipedia_client.o \
	src/net/wikipedia_self_test.o

AUDIO_OUTPUT_TEST_OBJS := \
	src/platform/audio_output.o \
	src/platform/shell_util.o \
	src/platform/audio_output_self_test.o

DISPLAY_TEST_OBJS := \
	src/ui/display/ui_chrome_model.o \
	src/ui/display/chrome_renderer.o \
	src/ui/display/chrome_rasterizer.o \
	src/ui/display/display_config.o \
	src/ui/display/display_backend.o \
	src/ui/display/display_composite.o \
	src/ui/display/display_fbdev.o \
	src/ui/display/display_st7789.o \
	src/ui/display/display_ncurses.o \
	src/ui/display/font8x8_basic.o \
	src/ui/ui_config.o \
	src/ui/menu_overlay.o \
	src/display_self_test.o

TELEMETRY_OBJS := \
	../shared/protocol.o \
	src/hardware/hardware_config.o \
	src/motion_gate.o \
	src/telemetry/crash_reporter.o \
	src/telemetry/drv2605l.o \
	src/telemetry/homing_service.o \
	src/telemetry/i2c_device.o \
	src/telemetry/limit_sensors.o \
	src/telemetry/ltc2944.o \
	src/telemetry/ram_text_persistence.o \
	src/telemetry/system_shutdown.o \
	src/telemetry/telemetry_bridge.o \
	src/telemetry/telemetry_config.o \
	src/telemetry/telemetry_sentinel.o \
	src/sentinel_main.o

LIBLOUIS_TEST_OBJS := \
	src/documents/liblouis_bridge_louis.o \
	src/documents/liblouis_self_test.o

.PHONY: all clean check check-liblouis keyboard ui motion-test host-chord-test wikipedia-test audio-output-test display-test liblouis-test sentinel connectd ui-test install a11y liblouis display

all: braillatron-ui braillatron-motion-test braillatron-motion-gate-sync-test braillatron-host-chord-test braillatron-wikipedia-test braillatron-audio-output-test braillatron-display-test braillatron-sentinel braillatron-connectd braillatron-ui-test

keyboard: braillatron-ui

ui: braillatron-ui

display:
	$(MAKE) BRAILLATRON_DISPLAY=1 braillatron-ui braillatron-display-test

a11y:
	$(MAKE) BRAILLATRON_A11Y=1 all

liblouis:
	$(MAKE) BRAILLATRON_LIBLOUIS=1 braillatron-ui braillatron-liblouis-test

liblouis-test: braillatron-liblouis-test

motion-test: braillatron-motion-test

motion-gate-sync-test: braillatron-motion-gate-sync-test

host-chord-test: braillatron-host-chord-test

wikipedia-test: braillatron-wikipedia-test

audio-output-test: braillatron-audio-output-test

display-test: braillatron-display-test

sentinel: braillatron-sentinel

connectd: braillatron-connectd

ui-test: braillatron-ui-test

check: all
	./braillatron-motion-test
	./braillatron-motion-gate-sync-test
	./braillatron-host-chord-test
	./braillatron-wikipedia-test
	./braillatron-audio-output-test
	./braillatron-display-test
	./braillatron-ui-test

check-liblouis:
	$(MAKE) BRAILLATRON_LIBLOUIS=1 braillatron-liblouis-test
	./braillatron-liblouis-test

install:
	../deploy/install.sh

braillatron-ui: $(UI_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_OBJS) $(LDFLAGS) $(BACKEND_LIBS) -o $@

braillatron-ui-test: $(UI_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_TEST_OBJS) $(LDFLAGS) $(BACKEND_LIBS) -o $@

braillatron-liblouis-test: $(LIBLOUIS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LIBLOUIS_TEST_OBJS) $(LDFLAGS) $(LOUIS_LIBS) -o $@

# Dedicated object so check-liblouis never reuses liblouis_bridge.o from a stub build.
src/documents/liblouis_bridge_louis.o: src/documents/liblouis_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DBRAILLATRON_LIBLOUIS -c $< -o $@

braillatron-motion-test: $(MOTION_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(MOTION_TEST_OBJS) -o $@

braillatron-motion-gate-sync-test: $(MOTION_GATE_SYNC_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(MOTION_GATE_SYNC_TEST_OBJS) -o $@

braillatron-host-chord-test: $(HOST_CHORD_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(HOST_CHORD_TEST_OBJS) -o $@

braillatron-wikipedia-test: $(WIKIPEDIA_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(WIKIPEDIA_TEST_OBJS) -o $@

braillatron-audio-output-test: $(AUDIO_OUTPUT_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(AUDIO_OUTPUT_TEST_OBJS) -o $@

braillatron-display-test: $(DISPLAY_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(DISPLAY_TEST_OBJS) $(LDFLAGS) $(DISPLAY_LIBS) -o $@

braillatron-sentinel: $(TELEMETRY_OBJS)
	$(CXX) $(CXXFLAGS) $(TELEMETRY_OBJS) -o $@

braillatron-connectd: $(CONNECTD_OBJS)
	$(CXX) $(CXXFLAGS) $(CONNECTD_OBJS) $(LDFLAGS) -o $@

../shared/protocol.o: ../shared/protocol.c ../shared/protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../shared/protocol.c -o $@

src/display_self_test.o: src/display_self_test.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/keyboard/%.o: src/keyboard/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/hardware/%.o: src/hardware/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/kinematics/%.o: src/kinematics/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/documents/%.o: src/documents/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/motion/%.o: src/motion/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/haptics/%.o: src/haptics/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/telemetry/%.o: src/telemetry/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/platform/%.o: src/platform/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/net/%.o: src/net/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/ui/%.o: src/ui/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/ui/apps/%.o: src/ui/apps/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/ui/backends/%.o: src/ui/backends/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/ui/display/%.o: src/ui/display/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(BACKEND_CXXFLAGS) -c $< -o $@

src/connect/%.o: src/connect/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

ALL_OBJS := $(sort $(UI_OBJS) $(UI_TEST_OBJS) $(MOTION_TEST_OBJS) $(HOST_CHORD_TEST_OBJS) $(WIKIPEDIA_TEST_OBJS) $(AUDIO_OUTPUT_TEST_OBJS) $(DISPLAY_TEST_OBJS) $(LIBLOUIS_TEST_OBJS) $(TELEMETRY_OBJS) $(CONNECTD_OBJS))

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) \
		braillatron-ui braillatron-ui-test braillatron-motion-test braillatron-motion-gate-sync-test braillatron-host-chord-test braillatron-wikipedia-test braillatron-audio-output-test braillatron-display-test braillatron-liblouis-test braillatron-sentinel braillatron-connectd

-include $(ALL_OBJS:.o=.d)
