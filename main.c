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
static int       scroll_offset = 0;
static char      cur_filename[FILENAME_MAX];
static char      tab_buffer[32];

static void init_x(void);
static void draw_background(void);
static void draw_cursor(int x, int y);
static void draw_text(XftColor *colour, int x, int y, const char *text, int len);
static void draw_buffer(void);
static void goto_end_of_line(void);
static void goto_start_of_line(void);
static void ensure_cursor_visible(void);
static void prev_line(void);
static void next_line(void);
static void forward_char(void);
static void backward_char(void);
static void load_or_create_file(void);
static void save_to_file(void);
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
  int cursor_ypos = 0, cursor_xpos, start = 0, i, len, y;

  draw_background();
  for (i = 0; i <= buf_len; ++i) {
    if (i == buf_len || text_buffer[i] == '\n') {
      len = i - start;
      if ((y = ch + (cursor_ypos - scroll_offset) * ch) >= 0 && y <= height) {
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
      }
      start = i + 1;
      ++cursor_ypos;
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
ensure_cursor_visible(void)
{
  int i, cursor_ypos = 0;

  for (i = 0; i < cursor; ++i) {
    if (text_buffer[i] == '\n')
        ++cursor_ypos;
  }
  if (cursor_ypos < scroll_offset)
    scroll_offset = cursor_ypos;
  else if (cursor_ypos >= scroll_offset + height / ch)
    scroll_offset = cursor_ypos - height / ch + 1;
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
  ensure_cursor_visible();
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
  ensure_cursor_visible();
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

void
load_or_create_file(void)
{
  FILE *fp;

  if ((fp = fopen(cur_filename, "r")) == NULL) {
    if ((fp = fopen(cur_filename, "w+")) == NULL) {
      fprintf(stderr, "could not create \"%s\"\n", cur_filename);
      exit(1);
    }
  } else {
    fseek(fp, 0, SEEK_END);
    buf_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    fread(text_buffer, 1, buf_len, fp);
  }
  fclose(fp);
}

void
save_to_file(void)
{
  FILE *fp;

  if ((fp = fopen(cur_filename, "w")) == NULL) {
    fprintf(stderr, "could not open \"%s\"\n", cur_filename);
    exit(1);
  }
  if (fwrite(text_buffer, 1, buf_len, fp) <= 0) {
    fprintf(stderr, "could not write to \"%s\"\n", cur_filename);
    exit(1);
  }
  fclose(fp);
}

void
handle_ctrl(KeySym key_sym)
{
  int start, len, i;

  switch (key_sym) {
  case XK_q:
    alive = 0;
    break;
  case XK_s:
    save_to_file();
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
    break;
  case XK_v:
    for (i = 0; i < height / ch; ++i)
      next_line();
    break;
  default:
    break;
  }
}

void
handle_meta(KeySym key_sym)
{
  int start, len, i;

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
  case XK_v:
    for (i = 0; i < height / ch; ++i)
      prev_line();
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
  strncpy(cur_filename, argv[1], FILENAME_MAX - 1);
  cur_filename[FILENAME_MAX - 1] = '\0';
  load_or_create_file();
  memset(tab_buffer, ' ', 32);
  init_x();
  event_loop();
  return 0;
}
