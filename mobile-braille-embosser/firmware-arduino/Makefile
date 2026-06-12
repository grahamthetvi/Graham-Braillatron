FQBN ?= arduino:avr:micro
PORT ?= /dev/ttyACM1

.PHONY: compile upload check-deps

compile:
	arduino-cli compile --fqbn $(FQBN) .

upload: compile
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) .

check-deps:
	arduino-cli core list | grep -q arduino:avr
