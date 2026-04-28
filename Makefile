CC      ?= clang
WARNS   := -Wall -Wextra -Wshadow -Wstrict-prototypes
CFLAGS  ?= -O2 -g -std=c11 -D_GNU_SOURCE
LDFLAGS ?=
PREFIX  ?= /usr/local

SRC_DIR := src
TP_DIR  := third_party
BUILD   := build

SRC_SOURCES := $(wildcard $(SRC_DIR)/*.c)
TP_SOURCES  := $(wildcard $(TP_DIR)/*.c)
SRC_OBJS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRC_SOURCES))
TP_OBJS     := $(patsubst $(TP_DIR)/%.c,$(BUILD)/tp/%.o,$(TP_SOURCES))
OBJS        := $(SRC_OBJS) $(TP_OBJS)

BIN := $(BUILD)/onet

.PHONY: all clean install

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(WARNS) -I$(SRC_DIR) -I$(TP_DIR) -c -o $@ $<

$(BUILD)/tp/%.o: $(TP_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(TP_DIR) -c -o $@ $<

clean:
	rm -rf $(BUILD)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/onet

install-systemd: install
	install -Dm644 contrib/systemd/onet.service          $(DESTDIR)/etc/systemd/system/onet.service
	install -Dm644 contrib/systemd/onet-watchdog.service $(DESTDIR)/etc/systemd/system/onet-watchdog.service
	@echo
	@echo "Now enable with:  systemctl daemon-reload && systemctl enable --now onet onet-watchdog"
