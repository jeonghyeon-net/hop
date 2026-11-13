/*
 * hop — XF86Tools 키로 작업공간을 전환한다.
 *
 *   Tools 탭            → 다음 작업공간 (마지막 다음은 처음)
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
static Atom a_current, a_count;
static int grab_failed;

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

static void switch_next(Time t)
{
	long count = get_cardinal(a_count, 1);
	long cur = get_cardinal(a_current, 0);
	if (count > 1)
		switch_to((cur + 1) % count, t);
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

	KeyCode tools = XKeysymToKeycode(dpy, XF86XK_Tools);
	if (!tools) {
		fprintf(stderr, "hop: XF86Tools 키를 찾을 수 없습니다\n");
		return 1;
	}

	/* 길게 눌렀을 때 Release 없이 Press만 반복되도록 한다 */
	XkbSetDetectableAutoRepeat(dpy, True, NULL);

	XSetErrorHandler(on_grab_error);
	XGrabKey(dpy, tools, AnyModifier, root, False, GrabModeAsync,
		 GrabModeAsync);
	XSync(dpy, False);
	if (grab_failed) {
		fprintf(stderr, "hop: 다른 프로그램이 이미 Tools 키를 "
				"사용 중입니다\n");
		return 1;
	}

	int held = 0, used = 0;
	for (;;) {
		XEvent ev;
		XNextEvent(dpy, &ev);

		if (ev.type == KeyPress) {
			KeyCode kc = ev.xkey.keycode;
			if (kc == tools) {
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
			   ev.xkey.keycode == tools && held) {
			if (!used)
				switch_next(ev.xkey.time);
			held = 0;
		}
	}
}
