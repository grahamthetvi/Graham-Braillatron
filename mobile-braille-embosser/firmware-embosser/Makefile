# Host build of the embosser pipeline. No ESP32 core and no stepper board.
CC := gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -I../shared

.PHONY: check clean

check: emboss_pipeline_test
	./emboss_pipeline_test

emboss_pipeline_test: emboss_pipeline_test.c emboss_pipeline.c emboss_pipeline.h ../shared/print_contract.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ emboss_pipeline_test.c emboss_pipeline.c

clean:
	rm -f emboss_pipeline_test
