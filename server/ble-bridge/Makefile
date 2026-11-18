# ble-bridge: one C file, linked to libsystemd (sd-bus) dynamically.

CC ?= cc
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra
SDBUS_CFLAGS := $(shell pkg-config --cflags libsystemd)
SDBUS_LIBS := $(shell pkg-config --libs libsystemd)

build/ble-bridge: ble-bridge.c Makefile
	@mkdir -p build
	$(CC) -std=c11 $(WARNINGS) $(CPPFLAGS) $(CFLAGS) $(SDBUS_CFLAGS) $(LDFLAGS) -o $@ ble-bridge.c $(SDBUS_LIBS) $(LDLIBS)

clean:
	rm -rf build

.PHONY: clean
