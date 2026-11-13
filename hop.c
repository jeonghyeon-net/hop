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

static Display *dpy;
static Window root;
#define MAX_DESKTOPS 64

static Atom a_current, a_count, a_clients, a_wm_desktop;
static int grab_failed;
static unsigned char is_tools[256];

static int on_grab_error(Display *d, XErrorEvent *e)
{
	(void)d;
	if (e->error_code == BadAccess)
		grab_failed = 1;
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
	ev.xclient.data.l[1] = t;
	XSendEvent(dpy, root, False,
		   SubstructureNotifyMask | SubstructureRedirectMask, &ev);
	XFlush(dpy);
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
			       &data) != Success || !data)
		return;

	Window *wins = (Window *)data;
	for (unsigned long i = 0; i < n; i++) {
		Atom t;
		int f;
		unsigned long m, a;
		unsigned char *d = NULL;

		if (XGetWindowProperty(dpy, wins[i], a_wm_desktop, 0, 1, False,
				       XA_CARDINAL, &t, &f, &m, &a,
				       &d) == Success && d && m == 1) {
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

	unsigned char occupied[MAX_DESKTOPS] = {0};
	/* 조회 중에 창이 닫혀 BadWindow가 나도 on_grab_error가 무시한다 */
	mark_occupied(occupied, count);

	for (long i = 1; i < count; i++) {
		long d = (cur + i) % count;
		if (occupied[d]) {
			switch_to(d, t);
			return;
		}
	}
}

/* 1~9 → 0~8, 0 → 9, 숫자가 아니면 -1 */
static int digit_index(KeyCode kc)
{
	KeySym ks = XkbKeycodeToKeysym(dpy, kc, 0, 0);
	if (ks >= XK_1 && ks <= XK_9)
		return (int)(ks - XK_1);
	if (ks == XK_0)
		return 9;
	return -1;
}

/*
 * XF86Tools가 붙은 키코드를 전부 찾아 잡는다. XF86Tools는 여러 키코드에
 * 매핑될 수 있으므로(예: 179, 191) XKeysymToKeycode가 돌려주는 첫 번째만이
 * 아니라 키맵 전체를 훑는다. 키맵이 바뀌면 다시 호출되므로 기존 grab은
 * 먼저 푼다. 잡은 키코드 수를 돌려준다.
 */
static int grab_tools(void)
{
	int min_kc, max_kc, n = 0;
	XDisplayKeycodes(dpy, &min_kc, &max_kc);

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (int kc = 0; kc < 256; kc++)
		is_tools[kc] = 0;

	grab_failed = 0;
	for (int kc = min_kc; kc <= max_kc; kc++) {
		if (XkbKeycodeToKeysym(dpy, kc, 0, 0) != XF86XK_Tools)
			continue;
		is_tools[kc] = 1;
		n++;
		XGrabKey(dpy, kc, AnyModifier, root, False, GrabModeAsync,
			 GrabModeAsync);
	}
	XSync(dpy, False);
	return n;
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
	XSetErrorHandler(on_grab_error);

	if (!grab_tools()) {
		fprintf(stderr, "hop: XF86Tools 키를 찾을 수 없습니다\n");
		return 1;
	}
	if (grab_failed) {
		fprintf(stderr, "hop: 다른 프로그램이 이미 Tools 키를 "
				"사용 중입니다\n");
		return 1;
	}

	int held = 0, used = 0;
	for (;;) {
		XEvent ev;
		XNextEvent(dpy, &ev);

		if (ev.type == MappingNotify) {
			/*
			 * 키보드를 새로 꽂거나 setxkbmap·xmodmap으로 키맵이
			 * 바뀌면 키코드가 달라질 수 있으니 다시 잡는다.
			 * 수정키(modifier) 변경은 grab에 영향이 없다.
			 */
			int keyboard = 0;
			do {
				XRefreshKeyboardMapping(&ev.xmapping);
				if (ev.xmapping.request == MappingKeyboard)
					keyboard = 1;
			} while (XCheckTypedEvent(dpy, MappingNotify, &ev));
			if (!keyboard)
				continue;
			held = 0;
			int n = grab_tools();
			if (!n)
				fprintf(stderr, "hop: 키맵이 바뀐 뒤 XF86Tools "
						"키를 찾을 수 없습니다\n");
			else if (grab_failed)
				fprintf(stderr, "hop: 키맵이 바뀐 뒤 Tools 키 "
						"일부를 잡지 못했습니다\n");
			continue;
		}

		if (ev.type == KeyPress) {
			KeyCode kc = ev.xkey.keycode;
			if (is_tools[kc]) {
				if (!held) { /* 자동 반복은 무시 */
					held = 1;
					used = 0;
				}
			} else if (held) {
				int idx = digit_index(kc);
				if (idx >= 0) {
					switch_to(idx, ev.xkey.time);
					used = 1;
				}
			}
		} else if (ev.type == KeyRelease &&
			   is_tools[ev.xkey.keycode] && held) {
			if (!used)
				switch_next(ev.xkey.time);
			held = 0;
		}
	}
}
