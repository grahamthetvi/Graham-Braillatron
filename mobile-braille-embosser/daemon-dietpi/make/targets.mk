.PHONY: all clean check check-liblouis keyboard ui motion-test host-chord-test wikipedia-test audio-output-test display-test remote-display-test liblouis-test timer-test dictionary-test spelling-test contacts-test music-test library-test gmail-test calculator-test sentinel connectd displayd connect-test ui-test install a11y liblouis display motion-gate-sync-test weather-test podcasts-test radio-test worthwhile-test list-tests

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
	$(foreach t,$(CHECK_TEST_BINS),./$(t)$(newline))

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

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d) $(ALL_BINS)

-include $(ALL_OBJS:.o=.d)
