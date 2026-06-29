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

# Use a dedicated liblouis object for linked targets so stub builds cannot reuse it.
ifeq ($(strip $(LOUIS_LIBS)),)
LIBLOUIS_BRIDGE_OBJ := src/documents/liblouis_bridge.o
else
LIBLOUIS_BRIDGE_OBJ := src/documents/liblouis_bridge_louis.o
endif

BACKEND_CXXFLAGS := $(LOUIS_CXXFLAGS) $(A11Y_CXXFLAGS)
BACKEND_LIBS := $(LOUIS_LIBS) $(A11Y_LIBS) -lsqlite3

DISPLAY_LIBS := -lncurses
DISPLAY_CXXFLAGS :=
ifeq ($(BRAILLATRON_DISPLAY),1)
DISPLAY_CXXFLAGS += -DBRAILLATRON_DISPLAY
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
	src/ui/display/chrome_frame.o \
	src/ui/display/remote_frame_publisher.o \
	src/display/frame_protocol.o \
	src/ui/display/display_config.o \
	src/ui/display/display_backend.o \
	src/ui/display/display_composite.o \
	src/ui/display/display_fbdev.o \
	src/ui/display/font8x8_basic.o \
	src/ui/display/display_st7789.o \
	src/ui/display/display_ncurses.o

DISPLAYD_OBJS := \
	src/display/frame_protocol.o \
	src/display/remote_display_config.o \
	src/display/pairing_auth.o \
	src/display/frame_subscriber.o \
	src/display/http_server.o \
	src/display/display_service.o \
	src/display/display_main.o \
	src/display/virtual_keyboard.o \
	src/connect/json_utils.o \
	src/connect/socket_server.o

REMOTE_DISPLAY_TEST_OBJS := \
	src/display/frame_protocol.o \
	src/display/remote_display_config.o \
	src/display/pairing_auth.o \
	src/display/display_client.o \
	src/connect/json_utils.o \
	src/connect/socket_server.o \
	src/display/remote_display_self_test.o \
	src/display/pairing_auth_self_test.o

PLATFORM_OBJS := \
	src/platform/device_status.o \
	src/platform/serial_link.o \
	src/platform/shell_util.o \
	src/platform/audio_output.o \
	src/platform/network_util.o

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
	src/documents/dictionary_store.o \
	src/documents/spelling_list_store.o \
	src/documents/contacts_store.o \
	src/documents/library_store.o \
	src/documents/edit_session.o \
	$(LIBLOUIS_BRIDGE_OBJ) \
	src/documents/paper_separator.o

MOTION_OBJS := \
	src/motion/motion_service.o

HAPTICS_OBJS := \
	src/haptics/morse_encoder.o

APP_OBJS := \
	src/ui/apps/app_registry.o \
	src/ui/apps/word_buffer_input.o \
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
	src/ui/apps/messages_app.o \
	src/ui/apps/dictionary_app.o \
	src/ui/apps/spelling_app.o \
	src/ui/apps/contacts_app.o \
	src/ui/apps/music_app.o \
	src/ui/apps/weather_app.o \
	src/ui/apps/podcasts_app.o \
	src/ui/apps/radio_app.o \
	src/ui/apps/gmail_app.o \
	src/ui/apps/timer_inline.o

CONNECT_COMMON_OBJS := \
	src/connect/connect_config.o \
	src/connect/connect_defaults.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/connect_job_queue.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/socket_server.o \
	src/connect/mpv_ipc.o \
	src/connect/mpv_service.o \
	src/connect/youtube_backend.o \
	src/connect/music_backend.o \
	src/connect/weather_backend.o \
	src/connect/rss_backend.o \
	src/connect/radio_backend.o \
	src/connect/library_backend.o \
	src/connect/worthwhile_backend.o \
	src/connect/gmail_backend.o \
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
	src/ui/layered_browse_list.o \
	src/ui/output_hub.o \
	src/ui/timer_service.o \
	src/ui/ui_app.o \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/event_writer.o \
	src/connect/connect_defaults.o \
	src/connect/connect_config.o \
	src/connect/subprocess.o \
	src/connect/gmail_backend.o \
	src/display/display_client.o \
	src/display/remote_display_config.o \
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
	src/ui/layered_browse_list.o \
	src/ui/output_hub.o \
	src/ui/timer_service.o \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/event_writer.o \
	src/connect/connect_config.o \
	src/connect/connect_defaults.o \
	src/connect/subprocess.o \
	src/connect/gmail_backend.o \
	src/display/display_client.o \
	src/display/remote_display_config.o \
	src/keyboard/focus_nav.o \
	src/ui_self_test.o

CONNECT_TEST_OBJS := \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/event_writer.o \
	src/connect/connect_client_self_test.o

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
	src/ui/display/chrome_frame.o \
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

TIMER_TEST_OBJS := \
	src/ui/timer_service.o \
	src/ui/timer_self_test.o

DICTIONARY_TEST_OBJS := \
	src/documents/dictionary_store.o \
	src/documents/dictionary_self_test.o

SPELLING_TEST_OBJS := \
	src/documents/spelling_list_store.o \
	src/documents/spelling_self_test.o

CONTACTS_TEST_OBJS := \
	src/documents/contacts_store.o \
	src/documents/contacts_self_test.o

MUSIC_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/mpv_ipc.o \
	src/connect/mpv_service.o \
	src/connect/music_backend.o \
	src/connect/music_self_test.o

WEATHER_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/event_writer.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/weather_backend.o \
	src/connect/weather_self_test.o

PODCASTS_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/mpv_ipc.o \
	src/connect/mpv_service.o \
	src/connect/rss_backend.o \
	src/connect/podcasts_self_test.o

RADIO_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/mpv_ipc.o \
	src/connect/mpv_service.o \
	src/connect/radio_backend.o \
	src/connect/radio_self_test.o

LIBRARY_TEST_OBJS := \
	src/documents/library_store.o \
	src/documents/library_self_test.o

LIBRARY_BACKEND_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/library_backend.o \
	src/connect/library_self_test.o

WORTHWHILE_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/worthwhile_backend.o \
	src/connect/worthwhile_self_test.o

GMAIL_TEST_OBJS := \
	src/connect/connect_config.o \
	src/connect/json_utils.o \
	src/connect/subprocess.o \
	src/connect/event_writer.o \
	src/connect/gmail_backend.o \
	src/connect/gmail_self_test.o

CALCULATOR_TEST_OBJS := \
	src/ui/apps/calculator_self_test.o

.PHONY: all clean check check-liblouis keyboard ui motion-test host-chord-test wikipedia-test audio-output-test display-test remote-display-test liblouis-test timer-test dictionary-test spelling-test contacts-test music-test library-test gmail-test calculator-test sentinel connectd displayd connect-test ui-test install a11y liblouis display

all: braillatron-ui braillatron-motion-test braillatron-motion-gate-sync-test braillatron-host-chord-test braillatron-wikipedia-test braillatron-audio-output-test braillatron-display-test braillatron-remote-display-test braillatron-calculator-test braillatron-sentinel braillatron-connectd braillatron-displayd braillatron-ui-test braillatron-connect-test

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

remote-display-test: braillatron-remote-display-test

sentinel: braillatron-sentinel

connectd: braillatron-connectd

displayd: braillatron-displayd

connect-test: braillatron-connect-test

ui-test: braillatron-ui-test

timer-test: braillatron-timer-test

dictionary-test: braillatron-dictionary-test

spelling-test: braillatron-spelling-test

contacts-test: braillatron-contacts-test

music-test: braillatron-music-test

weather-test: braillatron-weather-test

podcasts-test: braillatron-podcasts-test

radio-test: braillatron-radio-test

library-test: braillatron-library-test braillatron-library-backend-test braillatron-worthwhile-test

worthwhile-test: braillatron-worthwhile-test

gmail-test: braillatron-gmail-test

calculator-test: braillatron-calculator-test

check: all braillatron-timer-test braillatron-dictionary-test braillatron-spelling-test braillatron-contacts-test braillatron-music-test braillatron-weather-test braillatron-podcasts-test braillatron-radio-test braillatron-library-test braillatron-library-backend-test braillatron-worthwhile-test braillatron-gmail-test braillatron-calculator-test
	./braillatron-motion-test
	./braillatron-motion-gate-sync-test
	./braillatron-host-chord-test
	./braillatron-wikipedia-test
	./braillatron-audio-output-test
	./braillatron-display-test
	./braillatron-remote-display-test
	./braillatron-connect-test
	./braillatron-ui-test
	./braillatron-timer-test
	./braillatron-dictionary-test
	./braillatron-spelling-test
	./braillatron-contacts-test
	./braillatron-music-test
	./braillatron-weather-test
	./braillatron-podcasts-test
	./braillatron-radio-test
	./braillatron-library-test
	./braillatron-library-backend-test
	./braillatron-worthwhile-test
	./braillatron-gmail-test
	./braillatron-calculator-test

check-liblouis:
	$(MAKE) BRAILLATRON_LIBLOUIS=1 braillatron-liblouis-test
	./braillatron-liblouis-test

install:
	../deploy/install.sh

braillatron-ui: $(UI_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_OBJS) $(LDFLAGS) $(BACKEND_LIBS) -o $@

braillatron-ui-test: $(UI_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_TEST_OBJS) $(LDFLAGS) $(BACKEND_LIBS) -o $@

braillatron-connect-test: $(CONNECT_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(CONNECT_TEST_OBJS) $(LDFLAGS) -pthread -o $@

braillatron-liblouis-test: $(LIBLOUIS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LIBLOUIS_TEST_OBJS) $(LDFLAGS) $(LOUIS_LIBS) -o $@

braillatron-timer-test: $(TIMER_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TIMER_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-dictionary-test: $(DICTIONARY_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(DICTIONARY_TEST_OBJS) $(LDFLAGS) -lsqlite3 -o $@

braillatron-spelling-test: $(SPELLING_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(SPELLING_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-contacts-test: $(CONTACTS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(CONTACTS_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-music-test: $(MUSIC_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(MUSIC_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-weather-test: $(WEATHER_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(WEATHER_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-podcasts-test: $(PODCASTS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(PODCASTS_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-radio-test: $(RADIO_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(RADIO_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-library-test: $(LIBRARY_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LIBRARY_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-library-backend-test: $(LIBRARY_BACKEND_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LIBRARY_BACKEND_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-worthwhile-test: $(WORTHWHILE_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(WORTHWHILE_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-gmail-test: $(GMAIL_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(GMAIL_TEST_OBJS) $(LDFLAGS) -o $@

braillatron-calculator-test: $(CALCULATOR_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(CALCULATOR_TEST_OBJS) -o $@

# Dedicated object so liblouis-enabled targets never reuse stub liblouis_bridge.o.
src/documents/liblouis_bridge_louis.o: src/documents/liblouis_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LOUIS_CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

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

braillatron-displayd: $(DISPLAYD_OBJS)
	$(CXX) $(CXXFLAGS) $(DISPLAYD_OBJS) $(LDFLAGS) -pthread -o $@

braillatron-remote-display-test: $(REMOTE_DISPLAY_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(REMOTE_DISPLAY_TEST_OBJS) $(LDFLAGS) -pthread -o $@

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

src/display/%.o: src/display/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

ALL_OBJS := $(sort $(UI_OBJS) $(UI_TEST_OBJS) $(MOTION_TEST_OBJS) $(HOST_CHORD_TEST_OBJS) $(WIKIPEDIA_TEST_OBJS) $(AUDIO_OUTPUT_TEST_OBJS) $(DISPLAY_TEST_OBJS) $(REMOTE_DISPLAY_TEST_OBJS) $(LIBLOUIS_TEST_OBJS) $(TIMER_TEST_OBJS) $(DICTIONARY_TEST_OBJS) $(SPELLING_TEST_OBJS) $(CONTACTS_TEST_OBJS) $(MUSIC_TEST_OBJS) $(WEATHER_TEST_OBJS) $(PODCASTS_TEST_OBJS) $(RADIO_TEST_OBJS) $(LIBRARY_TEST_OBJS) $(LIBRARY_BACKEND_TEST_OBJS) $(WORTHWHILE_TEST_OBJS) $(GMAIL_TEST_OBJS) $(CONNECT_TEST_OBJS) $(TELEMETRY_OBJS) $(CONNECTD_OBJS) $(DISPLAYD_OBJS))

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) \
		braillatron-ui braillatron-ui-test braillatron-motion-test braillatron-motion-gate-sync-test braillatron-host-chord-test braillatron-wikipedia-test braillatron-audio-output-test braillatron-display-test braillatron-remote-display-test braillatron-liblouis-test braillatron-timer-test braillatron-dictionary-test braillatron-spelling-test braillatron-contacts-test braillatron-music-test braillatron-weather-test braillatron-podcasts-test braillatron-radio-test braillatron-library-test braillatron-library-backend-test braillatron-gmail-test braillatron-calculator-test braillatron-connect-test braillatron-sentinel braillatron-connectd braillatron-displayd

-include $(ALL_OBJS:.o=.d)
