PREFIX       ?= $(HOME)/.local
BINDIR       ?= $(PREFIX)/bin
AUTOSTARTDIR ?= $(HOME)/.config/autostart

CFLAGS ?= -O2 -Wall -Wextra
PKGS    = x11

hop: hop.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(shell pkg-config --cflags --libs $(PKGS))

install: hop
	install -Dm755 hop $(DESTDIR)$(BINDIR)/hop
	sed 's|@BINDIR@|$(BINDIR)|' hop.desktop.in > hop.desktop
	install -Dm644 hop.desktop $(DESTDIR)$(AUTOSTARTDIR)/hop.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/hop $(DESTDIR)$(AUTOSTARTDIR)/hop.desktop

clean:
	rm -f hop hop.desktop

.PHONY: install uninstall clean
