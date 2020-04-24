APP_NAME = e_transmission
PREFIX = /opt/mine

default: build/$(APP_NAME) module

CFLAGS := -Wall -Wextra -Wshadow -Wno-type-limits -g3 -O0 -Wpointer-arith -fvisibility=hidden

CFLAGS += -DAPP_NAME=\"$(APP_NAME)\" -DPREFIX=\"$(PREFIX)\"

EZPLUG_FLAGS := APP_NAME=$(APP_NAME) ICON_PATH=$(PREFIX)/share/$(APP_NAME)/icon.png BIN_CMD="$(PREFIX)/bin/$(APP_NAME)\ --socket"

build/$(APP_NAME): main.c
	mkdir -p $(@D)
	gcc -g $^ $(CFLAGS) `pkg-config --cflags --libs elementary` -o $@

module:
	make -C ezplug $(EZPLUG_FLAGS)

install: build/$(APP_NAME)
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/$(APP_NAME)
	install -c build/$(APP_NAME) $(PREFIX)/bin/
	install -c -m 644 images/* $(PREFIX)/share/$(APP_NAME)
	make -C ezplug install $(EZPLUG_FLAGS)

clean:
	rm -rf build/
	make -C ezplug clean
