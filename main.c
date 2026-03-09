#include <X11/Xlib.h>

#include <stdio.h>
#include <unistd.h>
#include <assert.h>

int
main(void)
{
  Display *dpy;
  Window w;
  GC gc;
  XEvent e;
  int black, white, scr_no, root;

  dpy = XOpenDisplay(NULL);
  assert(dpy);
  scr_no = DefaultScreen(dpy);
  root = DefaultRootWindow(dpy);
  black = BlackPixel(dpy, scr_no);
  white = WhitePixel(dpy, scr_no);
  w = XCreateSimpleWindow(dpy, root, 0, 0, 271, 67, 0, black, black);
  XSelectInput(dpy, w, KeyPressMask | ExposureMask | StructureNotifyMask);
  XMapWindow(dpy, w);
  gc = XCreateGC(dpy, w, 0, NULL);
  XSetForeground(dpy, gc, white);
  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  XDrawLine(dpy, w, gc, 90, 22, 180, 44);
  XFlush(dpy);
  for (;;) {
    XNextEvent(dpy, &e);
    if (e.type == Expose)
      XDrawLine(dpy, w, gc, 90, 22, 180, 44);
    if (e.type == KeyPress)
      break;
  }
  XFreeGC(dpy, gc);
  XDestroyWindow(dpy, w);
  XCloseDisplay(dpy);
  return 0;
}
