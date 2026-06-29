.PHONY: check check-liblouis list-tests ui firmware install clean dictionary-data

DICTIONARY_DB := mobile-braille-embosser/deploy/data/dictionary/en.sqlite

DAEMON_DIR := mobile-braille-embosser/daemon-dietpi
FIRMWARE_DIR := mobile-braille-embosser/firmware-arduino

check:
	$(MAKE) -C $(DAEMON_DIR) check

check-liblouis:
	$(MAKE) -C $(DAEMON_DIR) check-liblouis

list-tests:
	$(MAKE) -C $(DAEMON_DIR) list-tests

ui:
	$(MAKE) -C $(DAEMON_DIR) ui

firmware:
	$(MAKE) -C $(FIRMWARE_DIR) compile

install:
	$(MAKE) -C $(DAEMON_DIR) install

clean:
	$(MAKE) -C $(DAEMON_DIR) clean

dictionary-data:
	@mkdir -p "$(dir $(DICTIONARY_DB))"
	DB_PATH="$(CURDIR)/$(DICTIONARY_DB)" bash mobile-braille-embosser/deploy/install-dictionary-data.sh
