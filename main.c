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
static char text_buffer[65536], cur_filename[FILENAME_MAX]; /* keep on bss, cur_filename unused for now */
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
  const char *fontname = "fixed";

  fnt = XLoadFont(dpy, fontname);
  XSetFont(dpy, gc, fnt);
  fs = XLoadQueryFont(dpy, fontname);
  return XTextWidth(fs, "W", 1);
}

void
redraw_buffer(void)
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
draw_cursor(void)
{
  XFillRectangle(dpy, w, gc, x, y - offset, font_width / 3, 2 * font_width);
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
next_line(void)
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
forward_char(void)
{
  if (cursor < buf_len) {
    if (text_buffer[cursor] == '\n') {
      y += offset;
      x = offset;
    } else {
      x += font_width;
    }
    ++cursor;
  }
}

void
backward_char(void)
{
  int col = 0, i;

  if (cursor > 0) {
    if (text_buffer[cursor - 1] == '\n') {
      for (i = cursor - 1; i > 0 && text_buffer[i - 1] != '\n'; --i)
        ++col;
      y -= offset;
      x = offset + col * font_width;
    } else {
      x -= font_width;
    }
    --cursor;
  }
}

int
load_from_or_create_file(const char *filename)
{
  if ((file = fopen(filename, "r")) == NULL) {
    if ((file = fopen(filename, "w")) == NULL)
      return 0;
  } else {
    fseek(file, 0, SEEK_END);
    buf_len = ftell(file);
    fseek(file, 0, SEEK_SET);
    fread(text_buffer, 1, buf_len, file);
  }
  fclose(file);
  return 1;
}

int
save_to_file(const char *filename)
{
  if ((file = fopen(filename, "w")) == NULL)
    return 0;
  if (fwrite(text_buffer, 1, buf_len, file) <= 0)
    return 0;
  fclose(file);
  return 1;
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
    if (save_to_file(cur_filename) == 0)
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
    next_line();
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
      redraw_buffer();
      draw_cursor();
      break;
    case KeyPress:
      key_event = &e.xkey;
      key_sym = XLookupKeysym(key_event, 0);
      if ((key_event->state & ControlMask)) {
        handle_ctrl(key_sym);
      } else {
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
          next_line();
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
      }
      redraw_buffer();
      draw_cursor();
      XFlush(dpy);
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
  strcpy(cur_filename, argv[1]);
  if ((load_from_or_create_file(cur_filename)) == 0) {
    fprintf(stderr, "cannot open %s\n", cur_filename);
    exit(1);
  }
  init_window();
  init_gc();
  event_loop();
  cleanup();
  return 0;
}
