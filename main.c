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
  XSelectInput(dpy, w, KeyPressMask | ExposureMask | StructureNotifyMask);
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
delete_at_point(int x, int y, int font_width)
{
  XClearArea(dpy, w, x, y - 2 * font_width, 2 * font_width, 4 * font_width, False);
}

void
draw_cursor(int x, int y, int font_width)
{
  XFillRectangle(dpy, w, gc, x, y - 2 * font_width, font_width, 2 * font_width);
}

void
event_loop(void)
{
  XEvent e;
  KeySym key_sym;
  XKeyEvent *key_event;
  char buf[32];
  int len, font_width, step, x, y;

  font_width = init_font();
  x = y = step = 2 * font_width;
  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      draw_cursor(x, y, font_width);
      XDrawString(dpy, w, gc, step, step, text_buffer, buf_idx);
      break;
    case KeyPress:
      key_event = &e.xkey;
      key_sym = XLookupKeysym(key_event, 0);
      delete_at_point(x, y, font_width);
      if ((key_event->state & ControlMask)) {
        handle_ctrl(key_sym);
      } else {
        len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
        if (key_sym == XK_Return) {
          y += step;
          x = step;
          text_buffer[buf_idx++] = '\n';
        } else if (key_sym == XK_BackSpace) {
          if (x > step) {
            x -= font_width;
            delete_at_point(x, y, font_width);
            buf_idx -= 1;
          }
        } else if (len > 0) {
          memcpy(text_buffer + buf_idx, buf, len);
          buf_idx += len;
          XDrawString(dpy, w, gc, x, y, buf, len);
          x += font_width;
        }
        draw_cursor(x, y, font_width);
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
