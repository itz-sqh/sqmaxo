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
int buf_len = 0, cursor = 0;
int x, y, font_width, step;
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
  const char *fontname = "10x20";

  fnt = XLoadFont(dpy, fontname);
  XSetFont(dpy, gc, fnt);
  fs = XLoadQueryFont(dpy, fontname);
  return XTextWidth(fs, "W", 1);
}

void
redraw(void)
{
  int x_ = step, y_ = step, i;

  XClearWindow(dpy, w);
  for (i = 0; i < buf_len; ++i) {
    if (text_buffer[i] == '\n') {
      y_ += step;
      x_ = step;
    } else {
      XDrawString(dpy, w, gc, x_, y_, &text_buffer[i], 1);
      x_ += font_width;
    }
  }
}

void
delete_at_point(void)
{
  XClearArea(dpy, w, x, y - 2 * font_width, 2 * font_width, 4 * font_width, False);
}

void
draw_cursor(void)
{
  XFillRectangle(dpy, w, gc, x, y - 2 * font_width, font_width / 3, 2 * font_width);
}

void
handle_ctrl(KeySym key_sym)
{
  int i;
  int col = 0;
  int j;

  switch (key_sym) {
  case XK_q:
    printf("%.*s\n", buf_len, text_buffer);
    alive = 0;
    break;
  case XK_s:
    if ((fwrite(text_buffer, 1, buf_len, file)) <= 0)
      fprintf(stderr, "cannot write to file\n");
    break;
  case XK_a:
    for (i = cursor; text_buffer[i] != '\n' && i >= 0; --i)
      ;
    cursor = i + 1;
    x = step;
    redraw();
    draw_cursor();
    break;
  case XK_e:
    for (i = cursor; i < buf_len && text_buffer[i] != '\n'; ++i)
      ;
    cursor = i;
    for (j = cursor - 1; j >= 0 && text_buffer[j] != '\n'; --j)
      ++col;
    x = step + col * font_width;
    redraw();
    draw_cursor();
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
  int len;

  font_width = init_font();
  x = y = step = 2 * font_width;
  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      draw_cursor();
      XDrawString(dpy, w, gc, step, step, text_buffer, buf_len);
      break;
    case KeyPress:
      key_event = &e.xkey;
      key_sym = XLookupKeysym(key_event, 0);
      if ((key_event->state & ControlMask)) {
        handle_ctrl(key_sym);
      } else {
        delete_at_point();
        len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
        switch (key_sym) {
        case XK_Return:
          y += step;
          x = step;
          text_buffer[buf_len++] = '\n';
          ++cursor;
          break;
        case XK_BackSpace:
          if (x > step) {
            memmove(text_buffer + cursor - 1, text_buffer + cursor, buf_len - 1);
            x -= font_width;
            delete_at_point();
            --buf_len;
            --cursor;
          }
          break;
        case XK_Left:
          if (cursor > 0) {
            --cursor;
            x -= font_width;
          }
          break;
        case XK_Right:
          if (cursor < buf_len) {
            ++cursor;
            x += font_width;
          }
        default:
          if (len > 0) {
            memmove(text_buffer + cursor + len, text_buffer + cursor, buf_len - cursor);
            memcpy(text_buffer + cursor, buf, len);
            cursor += len;
            buf_len += len;
            x += font_width;
          }
        }
        redraw();
        draw_cursor();
        XFlush(dpy);
      }
      break;
    default:
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
