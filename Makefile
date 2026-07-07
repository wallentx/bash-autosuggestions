PREFIX ?= /usr/local
CC ?= cc
PKG_CONFIG ?= pkg-config

BASH_INCLUDE ?= $(shell $(PKG_CONFIG) --variable=includedir bash 2>/dev/null || printf '%s/include' "$${PREFIX:-/usr/local}")
READLINE_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags readline 2>/dev/null)
READLINE_LIBS ?= $(shell $(PKG_CONFIG) --libs readline 2>/dev/null || printf '%s' '-lreadline')

CFLAGS ?= -O2 -g
CPPFLAGS += -I$(BASH_INCLUDE) -I$(BASH_INCLUDE)/bash/include -I$(BASH_INCLUDE)/bash/builtins $(READLINE_CFLAGS)
LDFLAGS += -shared
LDLIBS += $(READLINE_LIBS)

TARGET = bash-autosuggestions.so
SRC = src/bash_autosuggestions.c

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC $(LDFLAGS) -o $@ $< $(LDLIBS)

test: $(TARGET)
	tests/smoke.sh

install: $(TARGET)
	install -d "$(DESTDIR)$(PREFIX)/lib/bash-autosuggestions"
	install -m 755 $(TARGET) "$(DESTDIR)$(PREFIX)/lib/bash-autosuggestions/$(TARGET)"
	install -m 644 bash-autosuggestions.bash "$(DESTDIR)$(PREFIX)/lib/bash-autosuggestions/bash-autosuggestions.bash"

clean:
	rm -f $(TARGET)
