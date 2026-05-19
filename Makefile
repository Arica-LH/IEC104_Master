CC ?= gcc
PREFIX ?= /usr/local
SYSCONFDIR ?= /etc
SYSTEMDDIR ?= /etc/systemd/system

TARGET := iec104-master
BUILD_DIR := build
SRC := $(shell find src -name '*.c' -type f | sort)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

CPPFLAGS ?=
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -pthread -Iinclude
LDFLAGS ?=
LDLIBS ?= -lm -pthread

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

install: all
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/sbin/$(TARGET)
	install -d $(DESTDIR)$(SYSCONFDIR)/iec104-master
	install -m 0644 config/iec104_master.conf $(DESTDIR)$(SYSCONFDIR)/iec104-master/iec104_master.conf
	install -d $(DESTDIR)$(SYSTEMDDIR)
	install -m 0644 systemd/iec104-master.service $(DESTDIR)$(SYSTEMDDIR)/iec104-master.service

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/sbin/$(TARGET)
	rm -f $(DESTDIR)$(SYSTEMDDIR)/iec104-master.service

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEP)
