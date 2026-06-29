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
