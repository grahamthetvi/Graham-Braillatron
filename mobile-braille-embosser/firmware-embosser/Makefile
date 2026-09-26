# Host build of the embosser pipeline and the ESP32-S3 / Spider link.
# No ESP32 core and no stepper board.
CC := gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror
CPPFLAGS += -I../shared

PIPELINE_SRCS := emboss_pipeline.c emboss_job.c
LINK_SRCS := $(PIPELINE_SRCS) emboss_radio.c emboss_spider.c \
	../shared/embosser_link.c ../shared/protocol.c

.PHONY: check clean

check: emboss_pipeline_test emboss_link_test
	./emboss_pipeline_test
	./emboss_link_test

emboss_pipeline_test: emboss_pipeline_test.c $(PIPELINE_SRCS) emboss_pipeline.h emboss_job.h \
		../shared/print_contract.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ emboss_pipeline_test.c $(PIPELINE_SRCS)

emboss_link_test: emboss_link_test.c $(LINK_SRCS) emboss_radio.h emboss_spider.h \
		../shared/embosser_link.h ../shared/protocol.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ emboss_link_test.c $(LINK_SRCS)

clean:
	rm -f emboss_pipeline_test emboss_link_test
