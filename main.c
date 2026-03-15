#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define CURSOR_COLOUR "#676767"
#define BG_COLOUR "#1f1f1f"
#define FG_COLOUR "#f1f1f1"
#define FONTNAME "monospace:size=14"

static Display  *dpy;
static int       screen;
static Window    w;
static GC        gc;
static XftDraw  *draw;
static XftFont  *font;
static XftColor  fg_colour;
static XftColor  bg_colour;
static XftColor  cursor_colour;
static int       width;
static int       height;
static int       cw;
static int       ch;
static int       x;
static int       y;
static int       alive = 1;
static char      text_buffer[65536];
static int       buf_len = 0;
static int       cursor = 0;
static FILE     *file;
static char      cur_filename[FILENAME_MAX];

static void init_x(void);
static void draw_buffer(void);
static void draw_cursor(void);
static void goto_end_of_line(void);
static void goto_start_of_line(void);
static void prev_line(void);
static void next_line(void);
static void forward_char(void);
static void backward_char(void);
static int  load_from_or_create_file(const char *filename);
static int  save_to_file(const char *filename);
static void handle_ctrl(KeySym key_sym);
static void event_loop(void);

void
init_x(void)
{
  if ((dpy = XOpenDisplay(NULL)) == NULL) {
    fprintf(stderr, "cannot open display\n");
    exit(1);
  }
  screen = DefaultScreen(dpy);
  width = 800;
  height = 600;
  w = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, width, height, 0, 0, 0);
  XSelectInput(dpy, w, ExposureMask | KeyPressMask | StructureNotifyMask);
  XMapWindow(dpy, w);
  gc = XCreateGC(dpy, w, 0, NULL);
  draw = XftDrawCreate(dpy, w, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
  if ((font = XftFontOpenName(dpy, screen, FONTNAME)) == NULL) {
    fprintf(stderr, "cannot load font\n");
    exit(1);
  }
  x = cw = font->max_advance_width;
  y = ch = font->ascent + font->descent;
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), FG_COLOUR, &fg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), BG_COLOUR, &bg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), CURSOR_COLOUR, &cursor_colour);
}

void
draw_buffer(void)
{
  int x_ = cw, y_ = ch, i;

  XSetWindowBackground(dpy, w, bg_colour.pixel);
  XClearWindow(dpy, w);
  for (i = 0; i < buf_len; ++i) {
    if (text_buffer[i] == '\n') {
      y_ += ch;
      x_ = cw;
    } else {
      XftDrawStringUtf8(draw, &fg_colour, font, x_, y_, (FcChar8 *)&text_buffer[i], 1);
      x_ += cw;
    }
  }
}

void
draw_cursor(void)
{
  XSetForeground(dpy, gc, cursor_colour.pixel);
  XFillRectangle(dpy, w, gc, x, y - font->ascent, cw, ch);
}

void
goto_end_of_line(void)
{
  int start = cursor;

  while (start > 0 && text_buffer[start - 1] != '\n')
    --start;
  while (cursor < buf_len && text_buffer[cursor] != '\n')
    ++cursor;
  x = cw + (cursor - start) * cw;
}

void
goto_start_of_line(void)
{
  while (cursor > 0 && text_buffer[cursor - 1] != '\n')
    --cursor;
  x = cw;
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
  y -= ch;
  x = cw + col * cw;
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
  y += ch;
  x = cw + col * cw;
}

void
forward_char(void)
{
  if (cursor < buf_len) {
    if (text_buffer[cursor] == '\n') {
      y += ch;
      x = cw;
    } else {
      x += cw;
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
      y -= ch;
      x = cw + col * cw;
    } else {
      x -= cw;
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

  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      draw_buffer();
      draw_cursor();
      break;
    case ConfigureNotify:
      width = e.xconfigure.width;
      height = e.xconfigure.height;
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
          y += ch;
          x = cw;
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
              y -= ch;
              goto_end_of_line();
            } else {
              x -= cw;
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
            x += cw;
          }
        }
      }
      draw_buffer();
      draw_cursor();
      XFlush(dpy);
      break;
    default:
      break;
    }
  }
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
  init_x();
  event_loop();
  return 0;
}
