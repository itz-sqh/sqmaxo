#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <string.h>

Display *dpy;
Window w;
GC gc;
int black, white;
int alive = 1;

void
init_window(void)
{
  int scr_no;

  dpy = XOpenDisplay(NULL);
  scr_no = DefaultScreen(dpy);
  black = BlackPixel(dpy, scr_no);
  white = WhitePixel(dpy, scr_no);
  w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 500, 400, 0, white, white);
  XSelectInput(dpy, w, KeyPressMask | StructureNotifyMask);
  XMapWindow(dpy, w);
}

void
init_gc(void)
{
  Font fnt;

  gc = XCreateGC(dpy, w, 0, NULL);
  XSetForeground(dpy, gc, black);
  fnt = XLoadFont(dpy, "fixed");
  XSetFont(dpy, gc, fnt);
}

void
handle_ctrl(KeySym key_sym)
{
  switch (key_sym) {
  case XK_q:
    alive = 0;
    break;
  default:
    break;
  }
}

void
event_loop(void)
{
  XEvent e;
  KeySym key_sym;
  XKeyEvent *key_event;
  char buf[32];
  int len, x = 10, y = 10, width = 10;

  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case KeyPress:
      key_event = &e.xkey;
      key_sym = XLookupKeysym(key_event, 0);
      if ((key_event->state & ControlMask)) {
        handle_ctrl(key_sym);
      } else {
        len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
        if (key_sym == XK_BackSpace) {
          if (x > width)
            x -= width;
          XClearArea(dpy, w, x, y - width, 2 * width, 2 * width, False);
        } else if (len > 0) {
          XDrawString(dpy, w, gc, x, y, buf, len);
          x += width;
        }
        XFlush(dpy);
      }
      break;
    }
  }
}

void
cleanup(void)
{
  XFreeGC(dpy, gc);
  XDestroyWindow(dpy, w);
  XCloseDisplay(dpy);
}

int
main(void)
{
  init_window();
  init_gc();
  event_loop();
  cleanup();
  return 0;
}
