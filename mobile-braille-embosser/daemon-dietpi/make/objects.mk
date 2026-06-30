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

PLATFORM_OBJS := $(call module_objs,src/platform,audio_output_self_test.cpp)
NET_OBJS := $(call module_objs,src/net,wikipedia_self_test.cpp)
KINEMATICS_OBJS := $(call module_objs,src/kinematics,)
MOTION_OBJS := $(call module_objs,src/motion,moonraker_self_test.cpp)
HAPTICS_OBJS := $(call module_objs,src/haptics,)

DOCUMENT_OBJS := \
	$(call module_objs,src/documents,liblouis_bridge.cpp liblouis_self_test.cpp dictionary_self_test.cpp spelling_self_test.cpp contacts_self_test.cpp library_self_test.cpp) \
	$(LIBLOUIS_BRIDGE_OBJ)

APP_OBJS := $(call module_objs,src/ui/apps,calculator_self_test.cpp)

CONNECT_COMMON_OBJS := $(call cpp_to_obj,$(filter-out %_self_test.cpp %connect_main.cpp %connect_service.cpp,$(wildcard src/connect/*.cpp)))

CONNECTD_OBJS := \
	$(CONNECT_COMMON_OBJS) \
	src/connect/connect_service.o \
	src/connect/connect_main.o

UI_CONNECT_CLIENT_OBJS := \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/event_writer.o \
	src/connect/connect_defaults.o \
	src/connect/connect_config.o \
	src/connect/subprocess.o \
	src/connect/gmail_backend.o

UI_KEYBOARD_OBJS := $(call module_objs,src/keyboard,host_chord_self_test.cpp)
UI_TEST_KEYBOARD_OBJS := $(UI_KEYBOARD_OBJS)

UI_COMMON_OBJS := \
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
	$(UI_CONNECT_CLIENT_OBJS) \
	src/display/display_client.o \
	src/display/remote_display_config.o

UI_OBJS := $(UI_COMMON_OBJS) src/ui_main.o src/ui/ui_app.o $(UI_KEYBOARD_OBJS)
UI_TEST_OBJS := $(UI_COMMON_OBJS) src/ui_self_test.o $(UI_TEST_KEYBOARD_OBJS)

CONNECT_TEST_OBJS := \
	src/connect/connect_client.o \
	src/connect/json_utils.o \
	src/connect/connect_async.o \
	src/connect/event_writer.o \
	src/connect/connect_client_self_test.o

MOTION_TEST_OBJS := $(KINEMATICS_OBJS) src/motion_gate.o src/motion_self_test.o

MOONRAKER_TEST_OBJS := \
	src/motion/klipper_config.o \
	src/motion/moonraker_client.o \
	src/connect/subprocess.o \
	src/connect/json_utils.o \
	src/motion/moonraker_self_test.o

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
	src/motion/klipper_config.o \
	src/motion/moonraker_client.o \
	src/connect/subprocess.o \
	src/connect/json_utils.o \
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

ALL_OBJS := $(sort $(UI_OBJS) $(UI_TEST_OBJS) $(MOTION_TEST_OBJS) $(MOONRAKER_TEST_OBJS) $(MOTION_GATE_SYNC_TEST_OBJS) $(HOST_CHORD_TEST_OBJS) $(WIKIPEDIA_TEST_OBJS) $(AUDIO_OUTPUT_TEST_OBJS) $(DISPLAY_TEST_OBJS) $(REMOTE_DISPLAY_TEST_OBJS) $(LIBLOUIS_TEST_OBJS) $(TIMER_TEST_OBJS) $(DICTIONARY_TEST_OBJS) $(SPELLING_TEST_OBJS) $(CONTACTS_TEST_OBJS) $(MUSIC_TEST_OBJS) $(WEATHER_TEST_OBJS) $(PODCASTS_TEST_OBJS) $(RADIO_TEST_OBJS) $(LIBRARY_TEST_OBJS) $(LIBRARY_BACKEND_TEST_OBJS) $(WORTHWHILE_TEST_OBJS) $(GMAIL_TEST_OBJS) $(CALCULATOR_TEST_OBJS) $(CONNECT_TEST_OBJS) $(TELEMETRY_OBJS) $(CONNECTD_OBJS) $(DISPLAYD_OBJS))

CHECK_TEST_BINS := \
	braillatron-motion-test \
	braillatron-moonraker-test \
	braillatron-motion-gate-sync-test \
	braillatron-host-chord-test \
	braillatron-wikipedia-test \
	braillatron-audio-output-test \
	braillatron-display-test \
	braillatron-remote-display-test \
	braillatron-connect-test \
	braillatron-ui-test \
	braillatron-timer-test \
	braillatron-dictionary-test \
	braillatron-spelling-test \
	braillatron-contacts-test \
	braillatron-music-test \
	braillatron-weather-test \
	braillatron-podcasts-test \
	braillatron-radio-test \
	braillatron-library-test \
	braillatron-library-backend-test \
	braillatron-worthwhile-test \
	braillatron-gmail-test \
	braillatron-calculator-test

ALL_BINS := \
	braillatron-ui braillatron-ui-test braillatron-motion-test braillatron-moonraker-test braillatron-motion-gate-sync-test \
	braillatron-host-chord-test braillatron-wikipedia-test braillatron-audio-output-test \
	braillatron-display-test braillatron-remote-display-test braillatron-liblouis-test \
	braillatron-timer-test braillatron-dictionary-test braillatron-spelling-test \
	braillatron-contacts-test braillatron-music-test braillatron-weather-test \
	braillatron-podcasts-test braillatron-radio-test braillatron-library-test \
	braillatron-library-backend-test braillatron-worthwhile-test braillatron-gmail-test \
	braillatron-calculator-test braillatron-connect-test braillatron-sentinel \
	braillatron-connectd braillatron-displayd
