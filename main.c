#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <unistd.h>

struct coords {
  int x1;
  int x2;
  int y1;
  int y2;
};

void
swap(int *x, int *y)
{
  int t = *x;

  *x = *y;
  *y = t;
}

void
centre(struct coords *c, int width, int height)
{
  c->x1 = width / 3;
  c->x2 = 2 * c->x1;
  c->y1 = height / 3;
  c->y2 = 2 * c->y1;
}

int
main(void)
{
  Display *dpy;
  Window w, root;
  GC gc;
  XEvent e;
  XWindowAttributes attr;
  int black, white;
  int scr_no;
  long input_mask = KeyPressMask | ExposureMask | StructureNotifyMask;
  KeySym k;
  struct coords c;

  dpy = XOpenDisplay(NULL);
  scr_no = DefaultScreen(dpy);
  root = DefaultRootWindow(dpy);
  black = BlackPixel(dpy, scr_no);
  white = WhitePixel(dpy, scr_no);
  w = XCreateSimpleWindow(dpy, root, 0, 0, 500, 400, 0, black, black);
  XSelectInput(dpy, w, input_mask);
  XMapWindow(dpy, w);
  gc = XCreateGC(dpy, w, 0, NULL);
  XSetForeground(dpy, gc, white);
  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  for (;;) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      XClearWindow(dpy, w);
      XGetWindowAttributes(dpy, w, &attr);
      centre(&c, attr.width, attr.height);
      XDrawLine(dpy, w, gc, c.x1, c.y1, c.x2, c.y2);
      XFlush(dpy);
      break;
    case KeyPress:
      switch (k = XLookupKeysym(&e.xkey, 0)) {
      case XK_r:
        XClearWindow(dpy, w);
        swap(&c.x1, &c.x2);
        XDrawLine(dpy, w, gc, c.x1, c.y1, c.x2, c.y2);
        XFlush(dpy);
        break;
      case XK_q:
        goto done;
      }
    }
  }
done:
  XFreeGC(dpy, gc);
  XDestroyWindow(dpy, w);
  XCloseDisplay(dpy);
  return 0;
}
