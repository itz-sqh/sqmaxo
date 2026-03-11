#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Display *dpy;
Window w;
GC gc;
int black, white;
int alive = 1;
char text_buffer[1024];
int buf_idx = 0;
FILE *file;

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
  gc = XCreateGC(dpy, w, 0, NULL);
  XSetForeground(dpy, gc, black);
}

int
init_font(void)
{
  Font fnt;
  XFontStruct *fs;
  const char *fontname = "fixed";

  fnt = XLoadFont(dpy, fontname);
  XSetFont(dpy, gc, fnt);
  fs = XLoadQueryFont(dpy, fontname);
  return XTextWidth(fs, "W", 1);
}

void
handle_ctrl(KeySym key_sym)
{
  switch (key_sym) {
  case XK_q:
    printf("%.*s\n", buf_idx, text_buffer);
    alive = 0;
    break;
  case XK_s:
    if ((fwrite(text_buffer, 1, buf_idx, file)) <= 0)
      fprintf(stderr, "cannot write to file\n");
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
  int len, width, x, y;

  width = init_font();
  x = y = 2 * width;
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
        if (key_sym == XK_Return) {
          y += 2 * width;
          x = 2 * width;
          text_buffer[buf_idx++] = '\n';
        } else if (key_sym == XK_BackSpace && x > 2 * width) {
          x -= width;
          XClearArea(dpy, w, x, y - 2 * width, width, 3 * width, False);
          buf_idx -= 1;
        } else if (len > 0) {
          memcpy(text_buffer + buf_idx, buf, len);
          buf_idx += len;
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
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "missing file name\n");
    return 1;
  }
  if ((file = fopen(argv[1], "w")) == NULL) {
    fprintf(stderr, "cannot open %s for writing\n", argv[1]);
    return 1;
  }
  init_window();
  init_gc();
  event_loop();
  cleanup();
  return 0;
}
