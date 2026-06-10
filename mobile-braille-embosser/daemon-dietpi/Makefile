CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2
CPPFLAGS += -I../shared -Isrc -MMD -MP
LDFLAGS ?= -levdev

# Set BRAILLATRON_A11Y=1 to link Speech Dispatcher, BRLTTY, and Vosk backends.
A11Y_LIBS :=
A11Y_CXXFLAGS :=
ifeq ($(BRAILLATRON_A11Y),1)
A11Y_CXXFLAGS += -DBRAILLATRON_A11Y
A11Y_LIBS += -lspeechd -lbrlapi -lvosk -llouis
endif

PLATFORM_OBJS := \
	src/platform/device_status.o \
	src/platform/serial_link.o

UI_OBJS := \
	$(PLATFORM_OBJS) \
	../shared/protocol.o \
	src/motion_gate.o \
	src/hardware/hardware_config.o \
	src/telemetry/drv2605l.o \
	src/telemetry/i2c_device.o \
	src/telemetry/telemetry_config.o \
	src/ui/ui_config.o \
	src/ui/backends/backend.o \
	src/ui/menu_overlay.o \
	src/ui/output_hub.o \
	src/ui/ui_app.o \
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
	../shared/protocol.o \
	src/hardware/hardware_config.o \
	src/telemetry/drv2605l.o \
	src/telemetry/i2c_device.o \
	src/telemetry/telemetry_config.o \
	src/ui/ui_config.o \
	src/ui/backends/backend.o \
	src/ui/menu_overlay.o \
	src/ui/output_hub.o \
	src/ui_self_test.o

KINEMATICS_OBJS := \
	src/kinematics/emboss_scheduler.o \
	src/kinematics/kinematics_config.o \
	src/kinematics/linkage_model.o \
	src/kinematics/motion_controller.o \
	src/kinematics/spatial_delay_line.o \
	src/kinematics/travel_log.o \
	src/motion_gate.o

MOTION_TEST_OBJS := $(KINEMATICS_OBJS) src/motion_self_test.o

HOST_CHORD_TEST_OBJS := \
	src/keyboard/host_chord_assembler.o \
	src/keyboard/host_chord_self_test.o

TELEMETRY_OBJS := \
	../shared/protocol.o \
	src/hardware/hardware_config.o \
	src/motion_gate.o \
	src/telemetry/drv2605l.o \
	src/telemetry/i2c_device.o \
	src/telemetry/limit_sensors.o \
	src/telemetry/ltc2944.o \
	src/telemetry/ram_text_persistence.o \
	src/telemetry/system_shutdown.o \
	src/telemetry/telemetry_config.o \
	src/telemetry/telemetry_sentinel.o \
	src/sentinel_main.o

.PHONY: all clean keyboard ui motion-test host-chord-test sentinel ui-test install a11y

all: braillatron-ui braillatron-motion-test braillatron-host-chord-test braillatron-sentinel braillatron-ui-test

keyboard: braillatron-ui

ui: braillatron-ui

a11y:
	$(MAKE) BRAILLATRON_A11Y=1 all

motion-test: braillatron-motion-test

host-chord-test: braillatron-host-chord-test

sentinel: braillatron-sentinel

ui-test: braillatron-ui-test

install:
	../deploy/install.sh

braillatron-ui: $(UI_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_OBJS) $(LDFLAGS) $(A11Y_LIBS) -o $@

braillatron-ui-test: $(UI_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(UI_TEST_OBJS) $(LDFLAGS) $(A11Y_LIBS) -o $@

braillatron-motion-test: $(MOTION_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(MOTION_TEST_OBJS) -o $@

braillatron-host-chord-test: $(HOST_CHORD_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(HOST_CHORD_TEST_OBJS) -o $@

braillatron-sentinel: $(TELEMETRY_OBJS)
	$(CXX) $(CXXFLAGS) $(TELEMETRY_OBJS) -o $@

../shared/protocol.o: ../shared/protocol.c ../shared/protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c ../shared/protocol.c -o $@

src/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

src/keyboard/%.o: src/keyboard/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

src/hardware/%.o: src/hardware/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

src/kinematics/%.o: src/kinematics/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/telemetry/%.o: src/telemetry/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/platform/%.o: src/platform/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

src/ui/%.o: src/ui/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

src/ui/backends/%.o: src/ui/backends/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(A11Y_CXXFLAGS) -c $< -o $@

ALL_OBJS := $(sort $(UI_OBJS) $(UI_TEST_OBJS) $(MOTION_TEST_OBJS) $(HOST_CHORD_TEST_OBJS) $(TELEMETRY_OBJS))

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) \
		braillatron-ui braillatron-ui-test braillatron-motion-test braillatron-host-chord-test braillatron-sentinel

-include $(ALL_OBJS:.o=.d)
