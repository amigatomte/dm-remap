# Makefile for dmremap-status userspace tool

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g
LDFLAGS ?=

# Target binary
TARGET = dmremap-status

# Source files
SOURCES = dmremap-status.c parser.c formatter.c json.c
OBJECTS = $(SOURCES:.c=.o)

# Man page
MAN_PAGE = man/dmremap-status.1

# Installation directories
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

.PHONY: all clean install uninstall help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c dmremap_status.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET) $(MAN_PAGE)
	install -D -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -D -m 0644 $(MAN_PAGE) $(DESTDIR)$(MANDIR)/$(notdir $(MAN_PAGE))
	@echo "Installation complete: $(DESTDIR)$(BINDIR)/$(TARGET)"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/$(notdir $(MAN_PAGE))
	@echo "Uninstallation complete"

help:
	@echo "dmremap-status build targets:"
	@echo "  make              - Build the tool"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make install      - Install to $(BINDIR)"
	@echo "  make uninstall    - Remove installed files"
	@echo ""
	@echo "Build variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  PREFIX=$(PREFIX)"
