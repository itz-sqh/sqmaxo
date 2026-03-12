#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

Display *dpy;
Window w;
GC gc;
int black, white;
int alive = 1;
char text_buffer[1024];
int buf_len = 0, cursor = 0;
int x, y, font_width, offset;
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
  int x_ = offset, y_ = offset, i;

  XClearWindow(dpy, w);
  for (i = 0; i < buf_len; ++i) {
    if (text_buffer[i] == '\n') {
      y_ += offset;
      x_ = offset;
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
goto_end_of_line(void)
{
  int start = cursor;

  while (start > 0 && text_buffer[start - 1] != '\n')
    --start;
  while (cursor < buf_len && text_buffer[cursor] != '\n')
    ++cursor;
  x = offset + (cursor - start) * font_width;
}

void
goto_start_of_line(void)
{
  while (cursor > 0 && text_buffer[cursor - 1] != '\n')
    --cursor;
  x = offset;
}

void
forward_char(void)
{
  if (cursor < buf_len) {
    ++cursor;
    x += font_width;
  }
}

void
backward_char(void)
{
  if (cursor > 0 && text_buffer[cursor - 1] != '\n') {
    --cursor;
    x -= font_width;
  }
}

void
prev_line(void)
{
  int i = cursor, col, col_cur = 0, col_prev = 0;

  while (i > 0 && text_buffer[i - 1] != '\n') {
    --i;
    ++col_cur;
  }
  if (i-- == 0)
    return;
  while (i > 0 && text_buffer[i - 1] != '\n') {
    --i;
    ++col_prev;
  }
  col = MIN(col_cur, col_prev);
  cursor = i + col;
  y -= offset;
  x = offset + col * font_width;
}

void
down_line(void)
{
  int i = cursor, col, col_cur = 0, col_next = 0;

  while (i > 0 && text_buffer[i - 1] != '\n') {
    --i;
    ++col_cur;
  }
  for (i = cursor; i < buf_len && text_buffer[i] != '\n'; ++i)
    ;
  if (i++ == buf_len)
    return;
  cursor = i;
  while (i < buf_len && text_buffer[i] != '\n') {
    ++i;
    ++col_next;
  }
  col = MIN(col_cur, col_next);
  cursor += col;
  y += offset;
  x = offset + col * font_width;
}

void
handle_ctrl(KeySym key_sym)
{
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
    goto_start_of_line();
    break;
  case XK_e:
    goto_end_of_line();
    break;
  case XK_f:
    forward_char();
    break;
  case XK_b:
    backward_char();
    break;
  case XK_p:
    prev_line();
    break;
  case XK_n:
    down_line();
    break;
  default:
    break;
  }
  redraw();
  draw_cursor();
}

void
event_loop(void)
{
  XEvent e;
  KeySym key_sym;
  XKeyEvent *key_event;
  char buf[32];
  int len;
  int backline;

  font_width = init_font();
  x = y = offset = 2 * font_width;
  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      draw_cursor();
      XDrawString(dpy, w, gc, offset, offset, text_buffer, buf_len);
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
          memmove(text_buffer + cursor + 1, text_buffer + cursor, buf_len - cursor);
          text_buffer[cursor] = '\n';
          ++buf_len;
          ++cursor;
          y += offset;
          x = offset;
          break;
        case XK_BackSpace:
          if (cursor > 0) {
            backline = 0;
            if (text_buffer[cursor - 1] == '\n')
              backline = 1;
            memmove(text_buffer + cursor - 1, text_buffer + cursor, buf_len - cursor);
            delete_at_point();
            --buf_len;
            --cursor;
            if (backline) {
              y -= offset;
              goto_end_of_line();
            } else {
              x -= font_width;
            }
          }
          break;
        case XK_Left:
          backward_char();
          break;
        case XK_Right:
          forward_char();
          break;
        case XK_Up:
          prev_line();
          break;
        case XK_Down:
          down_line();
          break;
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
