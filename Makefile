PREFIX       ?= $(HOME)/.local
BINDIR       ?= $(PREFIX)/bin
# 자동 실행 항목은 로그인한 사용자 기준이다. sudo로 설치하지 않는다.
AUTOSTARTDIR ?= $(HOME)/.config/autostart

CFLAGS ?= -O2 -Wall -Wextra
PKGS    = x11

hop: hop.c
	@pkg-config --exists $(PKGS) || { \
		echo "pkg-config로 $(PKGS)를 찾을 수 없습니다. libX11 개발 패키지를 설치하세요" >&2; \
		exit 1; }
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $$(pkg-config --cflags --libs $(PKGS))

# BINDIR이 바뀔 수 있으므로 자동 실행 항목은 설치할 때마다 새로 만든다.
install: hop
	install -Dm755 hop '$(DESTDIR)$(BINDIR)/hop'
	sed 's|@BINDIR@|$(BINDIR)|' hop.desktop.in > hop.desktop
	install -Dm644 hop.desktop '$(DESTDIR)$(AUTOSTARTDIR)/hop.desktop'

# 이 경로로 실행 중인 hop만 종료한다.
uninstall:
	-pkill -u "$$(id -u)" -xf '$(BINDIR)/hop'
	rm -f '$(DESTDIR)$(BINDIR)/hop' '$(DESTDIR)$(AUTOSTARTDIR)/hop.desktop'

clean:
	rm -f hop hop.desktop

.PHONY: install uninstall clean
