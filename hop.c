/*
 * hop — XF86Tools 키로 작업공간을 전환한다.
 *
 *   Tools 탭            → 창이 있는 다음 작업공간 (마지막 다음은 처음)
 *   Tools 누른 채 1~9,0 → 1~10번 작업공간
 *
 * Tools 키를 XGrabKey로 잡아 두면, 그 키가 눌려 있는 동안 X 서버가
 * 키보드 전체를 이 프로그램에 넘겨준다(passive grab). 그래서 함께 누른
 * 숫자는 포커스된 창에 입력되지 않는다. 전환은 EWMH
 * _NET_CURRENT_DESKTOP 요청으로 창 관리자에 맡긴다.
 */
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* xfwm4의 작업공간 상한은 32개다. 여유 있게 잡는다. */
#define MAX_DESKTOPS 64
/* 창 관리자가 _NET_CURRENT_DESKTOP을 갱신하기 전에 다시 눌렀다고 보는 시간 */
#define PENDING_MS 500

static Display *dpy;
static Window root;
static Atom a_current, a_count, a_clients, a_wm_desktop;

static int grab_failed;             /* 마지막 XGrabKey가 BadAccess였는지 */
static unsigned char is_tools[256]; /* XF86Tools가 붙은 키코드 */

/* 직전에 보낸 전환 요청 (빠르게 연달아 누를 때 쓴다) */
static long last_target = -1;
static Time last_time;

static int on_x_error(Display *d, XErrorEvent *e)
{
	/* grab 충돌은 호출한 쪽이 판단한다 */
	if (e->error_code == BadAccess) {
		grab_failed = 1;
		return 0;
	}
	/* 창 목록을 읽는 사이 창이 닫히는 건 정상이다 */
	if (e->error_code == BadWindow)
		return 0;

	char msg[256];
	XGetErrorText(d, e->error_code, msg, sizeof msg);
	fprintf(stderr, "hop: X 오류: %s (요청 %d)\n", msg, e->request_code);
	return 0;
}

static long get_cardinal(Atom prop, long fallback)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	long v = fallback;

	if (XGetWindowProperty(dpy, root, prop, 0, 1, False, XA_CARDINAL, &type,
			       &format, &n, &after, &data) == Success &&
	    data && n == 1 && format == 32)
		v = *(long *)data;
	if (data)
		XFree(data);
	return v;
}

static void switch_to(long target, Time t)
{
	long count = get_cardinal(a_count, 1);
	if (target < 0 || target >= count)
		return;

	XEvent ev = {0};
	ev.xclient.type = ClientMessage;
	ev.xclient.window = root;
	ev.xclient.message_type = a_current;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = target;
	ev.xclient.data.l[1] = (long)t;
	XSendEvent(dpy, root, False,
		   SubstructureNotifyMask | SubstructureRedirectMask, &ev);
	XFlush(dpy);

	last_target = target;
	last_time = t;
}

/*
 * 창이 하나라도 있는 작업공간을 occupied[]에 표시한다. 모든 작업공간에
 * 붙어 있는 창(_NET_WM_DESKTOP == 0xFFFFFFFF, 예: 바탕화면·패널)은
 * 어느 작업공간을 "사용 중"으로 만들지 않으므로 세지 않는다.
 */
static void mark_occupied(unsigned char *occupied, long count)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, root, a_clients, 0, 4096, False,
			       XA_WINDOW, &type, &format, &n, &after,
			       &data) != Success || !data) {
		if (data)
			XFree(data);
		return;
	}
	if (type != XA_WINDOW || format != 32) {
		XFree(data);
		return;
	}

	Window *wins = (Window *)data;
	for (unsigned long i = 0; i < n; i++) {
		Atom t;
		int f;
		unsigned long m, a;
		unsigned char *d = NULL;

		/* 조회 중 창이 닫혀 BadWindow가 나면 실패로 돌아온다 */
		if (XGetWindowProperty(dpy, wins[i], a_wm_desktop, 0, 1, False,
				       XA_CARDINAL, &t, &f, &m, &a,
				       &d) == Success && d && m == 1 &&
		    f == 32) {
			/*
			 * Xlib은 format 32 값을 long으로 부호 확장해 주므로
			 * 0xFFFFFFFF는 -1로 온다. 범위 밖 값은 모두 건너뛴다.
			 */
			long desk = *(long *)d;
			if (desk >= 0 && desk < count)
				occupied[desk] = 1;
		}
		if (d)
			XFree(d);
	}
	XFree(data);
}

/* 창이 있는 다음 작업공간으로 간다. 없으면 아무것도 하지 않는다. */
static void switch_next(Time t)
{
	long count = get_cardinal(a_count, 1);
	long cur = get_cardinal(a_current, 0);
	if (count < 2 || count > MAX_DESKTOPS)
		return;
	if (cur < 0 || cur >= count)
		cur = 0;

	/*
	 * 방금 보낸 요청을 창 관리자가 아직 반영하지 않았으면 그 대상을
	 * 현재 위치로 본다. 그래야 빠르게 두 번 눌렀을 때 한 번이 사라지지
	 * 않는다.
	 */
	if (last_target >= 0 && last_target < count && cur != last_target &&
	    (unsigned int)(t - last_time) < PENDING_MS) /* 서버 시각은 32비트 */
		cur = last_target;

	unsigned char occupied[MAX_DESKTOPS] = {0};
	mark_occupied(occupied, count);

	for (long i = 1; i < count; i++) {
		long d = (cur + i) % count;
		if (occupied[d]) {
			switch_to(d, t);
			return;
		}
	}
}

/*
 * 1~9 → 0~8, 0 → 9, 숫자가 아니면 -1.
 * 숫자가 Shift 단계에 있는 배열(AZERTY 등)과 키패드도 받도록
 * 기본 그룹의 1·2단계를 모두 본다.
 */
static int digit_index(KeyCode kc)
{
	for (unsigned int level = 0; level < 2; level++) {
		KeySym ks = XkbKeycodeToKeysym(dpy, kc, 0, (int)level);
		if (ks >= XK_1 && ks <= XK_9)
			return (int)(ks - XK_1);
		if (ks == XK_0)
			return 9;
		if (ks >= XK_KP_1 && ks <= XK_KP_9)
			return (int)(ks - XK_KP_1);
		if (ks == XK_KP_0)
			return 9;
	}
	return -1;
}

/*
 * XF86Tools가 붙은 키코드를 전부 찾아 잡는다. XF86Tools는 여러 키코드에
 * 매핑될 수 있으므로(예: 179, 191) XKeysymToKeycode가 돌려주는 첫 번째만이
 * 아니라 키맵 전체를 훑는다.
 *
 * 키보드 장치가 바뀔 때마다(다른 인터페이스의 키를 누르기만 해도)
 * MappingNotify가 오므로, 대상 키코드가 실제로 달라졌을 때만 다시 잡는다.
 * force가 참이면 무조건 다시 잡는다.
 *
 * 돌려주는 값: XF86Tools 키코드 수. *n_grabbed에는 실제로 잡은 수.
 */
static int grab_tools(int force, int *n_grabbed)
{
	unsigned char next[256] = {0};
	int min_kc, max_kc, n = 0;

	XDisplayKeycodes(dpy, &min_kc, &max_kc);
	for (int kc = min_kc; kc <= max_kc && kc < 256; kc++) {
		if (XkbKeycodeToKeysym(dpy, (KeyCode)kc, 0, 0) == XF86XK_Tools) {
			next[kc] = 1;
			n++;
		}
	}

	if (!force && memcmp(next, is_tools, sizeof next) == 0) {
		*n_grabbed = -1; /* 바뀐 게 없어 손대지 않음 */
		return n;
	}

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	memcpy(is_tools, next, sizeof next);

	int ok = 0;
	for (int kc = min_kc; kc <= max_kc && kc < 256; kc++) {
		if (!is_tools[kc])
			continue;
		grab_failed = 0;
		XGrabKey(dpy, kc, AnyModifier, root, False, GrabModeAsync,
			 GrabModeAsync);
		XSync(dpy, False); /* 키코드별로 성공 여부를 가린다 */
		if (grab_failed)
			fprintf(stderr, "hop: 키코드 %d은 다른 프로그램이 "
					"사용 중입니다\n", kc);
		else
			ok++;
	}
	*n_grabbed = ok;
	return n;
}

/* 잡아 둔 키 중 아직 물리적으로 눌려 있는 게 있는지 */
static int any_down(const unsigned char *keys)
{
	int none = 1;
	for (int kc = 0; kc < 256 && none; kc++)
		none = !keys[kc];
	if (none)
		return 0; /* 왕복 요청을 아낀다 */

	char map[32];
	XQueryKeymap(dpy, map);
	for (int kc = 0; kc < 256; kc++)
		if (keys[kc] && (map[kc / 8] & (1 << (kc % 8))))
			return 1;
	return 0;
}

int main(void)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "hop: X 디스플레이를 열 수 없습니다\n");
		return 1;
	}
	root = DefaultRootWindow(dpy);
	a_current = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	a_count = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	a_clients = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	a_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);

	/* 길게 눌렀을 때 Release 없이 Press만 반복되도록 한다 */
	XkbSetDetectableAutoRepeat(dpy, True, NULL);
	XSetErrorHandler(on_x_error);

	int grabbed;
	int n = grab_tools(1, &grabbed);
	if (!n) {
		/* 나중에 키맵에 추가될 수 있으니 MappingNotify를 기다린다 */
		fprintf(stderr, "hop: XF86Tools 키가 키맵에 없습니다. "
				"키맵이 바뀌기를 기다립니다\n");
	} else if (!grabbed) {
		fprintf(stderr, "hop: 다른 프로그램이 이미 Tools 키를 "
				"사용 중입니다\n");
		return 1;
	}

	KeyCode held_kc = 0;         /* 누르고 있는 Tools 키코드, 없으면 0 */
	int used = 0;                /* 누르는 동안 다른 키를 눌렀는지 */
	int kbd = 0;                 /* 키보드 전체를 직접(active) 잡고 있는지 */
	int warned = 0;              /* "키맵에 없음" 메시지를 이미 냈는지 */
	unsigned char others[256];   /* Tools와 함께 누른 키들 */
	memset(others, 0, sizeof others);

	for (;;) {
		XEvent ev;
		XNextEvent(dpy, &ev);

		if (ev.type == MappingNotify) {
			/*
			 * 키보드를 새로 꽂거나 setxkbmap·xmodmap으로 키맵이
			 * 바뀌면 키코드가 달라질 수 있으니 다시 잡는다.
			 * 누르고 있는 상태(held_kc)는 그대로 둔다. 다른 장치의
			 * 키를 누를 때마다 이 이벤트가 끼어들기 때문이다.
			 */
			XRefreshKeyboardMapping(&ev.xmapping);
			if (ev.xmapping.request != MappingKeyboard)
				continue;
			n = grab_tools(0, &grabbed);
			if (!n) {
				if (!warned)
					fprintf(stderr, "hop: 키맵이 바뀐 뒤 "
							"XF86Tools 키를 찾을 수 "
							"없습니다\n");
				warned = 1;
				continue;
			}
			warned = 0;
			if (grabbed == 0)
				fprintf(stderr, "hop: 키맵이 바뀐 뒤 Tools 키를 "
						"잡지 못했습니다\n");
			continue;
		}

		if (ev.type == KeyPress) {
			KeyCode kc = (KeyCode)ev.xkey.keycode; /* X 키코드는 8~255 */
			if (!held_kc && is_tools[kc] && !others[kc]) {
				held_kc = kc;
				used = 0;
				/*
				 * passive grab은 Tools를 떼는 순간 풀린다. 그 뒤
				 * 다시 잡기까지의 틈에 숫자 자동 반복이 새지 않도록
				 * 처음부터 키보드를 직접 잡아 둔다.
				 */
				if (!kbd) {
					memset(others, 0, sizeof others);
					if (XGrabKeyboard(dpy, root, False,
							  GrabModeAsync, GrabModeAsync,
							  ev.xkey.time) == GrabSuccess)
						kbd = 1;
				}
			} else if (held_kc) {
				/* Tools 자동 반복, 이미 누른 키의 자동 반복은 무시 */
				if (kc == held_kc || others[kc])
					continue;
				/*
				 * 숫자가 아닌 키를 눌러도 탭으로 치지 않는다.
				 * 그 키는 grab 중이라 창에 가지 않고 버려진다.
				 */
				others[kc] = 1;
				used = 1;
				int idx = digit_index(kc);
				if (idx >= 0)
					switch_to(idx, ev.xkey.time);
			} else if (kbd) {
				others[kc] = 1; /* Tools를 뗀 뒤의 입력도 삼킨다 */
			}
			continue;
		}

		if (ev.type != KeyRelease)
			continue;

		KeyCode kc = (KeyCode)ev.xkey.keycode; /* X 키코드는 8~255 */
		int tap = 0;
		if (held_kc && kc == held_kc) {
			tap = !used;
			held_kc = 0;
		} else if (held_kc || kbd) {
			others[kc] = 0;
		} else {
			continue;
		}
		if (held_kc)
			continue;

		/*
		 * Tools를 뗐다. 함께 누른 키가 아직 눌려 있으면 그 자동 반복이
		 * 창에 들어가지 않도록 다 떨어질 때까지 키보드를 계속 잡는다.
		 */
		if (any_down(others)) {
			if (!kbd && XGrabKeyboard(dpy, root, False, GrabModeAsync,
						  GrabModeAsync,
						  ev.xkey.time) == GrabSuccess)
				kbd = 1;
		} else if (kbd) {
			XUngrabKeyboard(dpy, ev.xkey.time);
			XFlush(dpy);
			kbd = 0;
		}

		/* 키보드를 먼저 놓고 전환해야 그사이 입력이 버려지지 않는다 */
		if (tap)
			switch_next(ev.xkey.time);
	}
}
