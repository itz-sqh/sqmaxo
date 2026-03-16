#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ALT_MASK 8192

#define CURSOR_COLOUR "#676767"
#define BG_COLOUR "#1f1f1f"
#define FG_COLOUR "#f1f1f1"
#define FONTNAME "monospace:size=14"
#define TABSTOP 2

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
static int       alive = 1;
static char      text_buffer[65536];
static int       buf_len = 0;
static int       cursor = 0;
static FILE     *file;
static char      cur_filename[FILENAME_MAX];
static char      tab_buffer[32];

static void init_x(void);
static void draw_background(void);
static void draw_cursor(int x, int y);
static void draw_text(XftColor *colour, int x, int y, const char *text, int len);
static void draw_buffer(void);
static void goto_end_of_line(void);
static void goto_start_of_line(void);
static void prev_line(void);
static void next_line(void);
static void forward_char(void);
static void backward_char(void);
static int  load_from_or_create_file(const char *filename);
static int  save_to_file(const char *filename);
static void handle_ctrl(KeySym key_sym);
static void handle_meta(KeySym key_sym);
static void insert_text(char *text, int len);
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
  cw = font->max_advance_width;
  ch = font->ascent + font->descent;
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), FG_COLOUR, &fg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), BG_COLOUR, &bg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), CURSOR_COLOUR, &cursor_colour);
}

void
draw_background(void)
{
  XSetForeground(dpy, gc, bg_colour.pixel);
  XFillRectangle(dpy, w, gc, 0, 0, width, height);
}

void
draw_cursor(int x, int y)
{
  XSetForeground(dpy, gc, cursor_colour.pixel);
  XFillRectangle(dpy, w, gc, x, y - font->ascent, cw, ch);
}

void
draw_text(XftColor *colour, int x, int y, const char *text, int len)
{
  if (len > 0)
    XftDrawStringUtf8(draw, colour, font, x, y, (FcChar8 *)text, len);
}

void
draw_buffer(void)
{
  int line = 0, cursor_xpos, start = 0, i, len, y;

  draw_background();
  for (i = 0; i <= buf_len; ++i) {
    if (i == buf_len || text_buffer[i] == '\n') {
      len = i - start;
      y = ch + line * ch;
      if (cursor >= start && cursor <= i) {
        cursor_xpos = cursor - start;
        draw_text(&fg_colour, cw, y, text_buffer + start, cursor_xpos);
        draw_cursor(cw + cursor_xpos * cw, y);
        if (cursor_xpos < len) {
          draw_text(&bg_colour, cw + cursor_xpos * cw, y, text_buffer + start + cursor_xpos, 1);
          draw_text(&fg_colour, cw + (cursor_xpos + 1) * cw, y, text_buffer + start + cursor_xpos + 1, len - cursor_xpos - 1);
        }
      } else {
        draw_text(&fg_colour, cw, y, text_buffer + start, len);
      }
      start = i + 1;
      ++line;
    }
  }
}

void
goto_end_of_line(void)
{
  while (cursor < buf_len && text_buffer[cursor] != '\n')
    ++cursor;
}

void
goto_start_of_line(void)
{
  while (cursor > 0 && text_buffer[cursor - 1] != '\n')
    --cursor;
}

void
prev_line(void)
{
  int i, col_cur = 0, col_prev = 0;

  for (i = cursor; i > 0 && text_buffer[i - 1] != '\n'; --i)
    ++col_cur;
  if (i-- == 0)
    return;
  for (; i > 0 && text_buffer[i - 1] != '\n'; --i)
    ++col_prev;
  cursor = i + MIN(col_cur, col_prev);
}

void
next_line(void)
{
  int i, col_cur = 0, col_next = 0;

  for (i = cursor; i > 0 && text_buffer[i - 1] != '\n'; --i)
    ++col_cur;
  for (i = cursor; i < buf_len && text_buffer[i] != '\n'; ++i)
    ;
  if (i++ == buf_len)
    return;
  cursor = i;
  for (; i < buf_len && text_buffer[i] != '\n'; ++i)
    ++col_next;
  cursor += MIN(col_cur, col_next);
}

void
forward_char(void)
{
  if (cursor < buf_len)
    ++cursor;
}

void
backward_char(void)
{
  if (cursor > 0)
    --cursor;
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
  int start, len;

  switch (key_sym) {
  case XK_q:
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
  case XK_d:
    if (cursor < buf_len) {
      ++cursor;
      insert_text(NULL, -1);
    }
    break;
  case XK_k:
    if (cursor < buf_len && text_buffer[cursor] == '\n') {
      ++cursor;
      insert_text(NULL, -1);
    } else {
      start = cursor;
      while (cursor < buf_len && text_buffer[cursor] != '\n')
        ++cursor;
      len = cursor - start;
      insert_text(NULL, -len);
    }
  default:
    break;
  }
}

void
handle_meta(KeySym key_sym)
{
  int start, len;

  switch (key_sym) {
  case XK_f:
    while (cursor < buf_len && isspace(text_buffer[cursor]))
      ++cursor;
    while (cursor < buf_len && !isspace(text_buffer[cursor]))
      ++cursor;
    break;
  case XK_b:
    while (cursor > 0 && isspace(text_buffer[cursor - 1]))
      --cursor;
    while (cursor > 0 && !isspace(text_buffer[cursor - 1]))
      --cursor;
    break;
  case XK_d:
    start = cursor;
    while (cursor < buf_len && isspace(text_buffer[cursor]))
      ++cursor;
    while (cursor < buf_len && !isspace(text_buffer[cursor]))
      ++cursor;
    len = cursor - start;
    insert_text(NULL, -len);
    break;
  case XK_BackSpace:
    start = cursor;
    while (cursor > 0 && isspace(text_buffer[cursor - 1]))
      --cursor;
    while (cursor > 0 && !isspace(text_buffer[cursor - 1]))
      --cursor;
    len = start - cursor;
    cursor = start;
    insert_text(NULL, -len);
    break;
  default:
    break;
  }
}

void
insert_text(char *text, int len)
{
  memmove(text_buffer + cursor + len, text_buffer + cursor, buf_len - cursor);
  if (text != NULL)
    memcpy(text_buffer + cursor, text, len);
  buf_len += len;
  cursor += len;
}

void
event_loop(void)
{
  XEvent e;
  KeySym key_sym;
  XKeyEvent *key_event;
  char buf[32];
  int len;

  do {
    XNextEvent(dpy, &e);
  } while (e.type != MapNotify);
  while (alive) {
    XNextEvent(dpy, &e);
    switch (e.type) {
    case Expose:
      draw_buffer();
      break;
    case ConfigureNotify:
      width = e.xconfigure.width;
      height = e.xconfigure.height;
      break;
    case KeyPress:
      key_event = &e.xkey;
      key_sym = XLookupKeysym(key_event, 0);
      if (key_event->state & ControlMask) {
        handle_ctrl(key_sym);
      } else if (key_event->state & (Mod1Mask | ALT_MASK)) {
        handle_meta(key_sym);
      } else {
        len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
        switch (key_sym) {
        case XK_Return:
          insert_text("\n", 1);
          break;
        case XK_Tab:
          insert_text(tab_buffer, TABSTOP);
          break;
        case XK_BackSpace:
          if (cursor > 0)
            insert_text(NULL, -1);
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
          if (len > 0)
            insert_text(buf, len);
        }
      }
      draw_buffer();
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
  memset(tab_buffer, ' ', 32);
  init_x();
  event_loop();
  return 0;
}
