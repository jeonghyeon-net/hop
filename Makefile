PREFIX       ?= $(HOME)/.local
BINDIR       ?= $(PREFIX)/bin
# 자동 실행 항목은 로그인한 사용자 기준이다. sudo로 설치하지 않는다.
AUTOSTARTDIR ?= $(HOME)/.config/autostart

CFLAGS ?= -O2 -Wall -Wextra
PKGS    = x11

X11_FLAGS := $(shell pkg-config --cflags --libs $(PKGS))
ifeq ($(X11_FLAGS),)
$(error pkg-config로 $(PKGS)를 찾을 수 없습니다. libX11 개발 패키지를 설치하세요)
endif

hop: hop.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(X11_FLAGS)

hop.desktop: hop.desktop.in
	sed 's|@BINDIR@|$(BINDIR)|' $< > $@

install: hop hop.desktop
	install -Dm755 hop $(DESTDIR)$(BINDIR)/hop
	install -Dm644 hop.desktop $(DESTDIR)$(AUTOSTARTDIR)/hop.desktop

uninstall:
	-pkill -x hop
	rm -f $(DESTDIR)$(BINDIR)/hop $(DESTDIR)$(AUTOSTARTDIR)/hop.desktop

clean:
	rm -f hop hop.desktop

.PHONY: install uninstall clean
