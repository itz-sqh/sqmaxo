#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define SQBUF_IMPLEMENTATION
#include "sqbuf.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define ALT_MASK 8192

#define CURSOR_COLOUR "#676767"
#define BG_COLOUR "#1f1f1f"
#define FG_COLOUR "#f1f1f1"
#define FONTNAME "monospace:size=14"
#define TABSTOP 2
#define MAX_BUF_LEN 65536

static Display		*dpy;
static int		 screen;
static Window		 w;
static GC		 gc;
static XftDraw		*draw;
static XftFont		*font;
static XftColor		 fg_colour;
static XftColor		 bg_colour;
static XftColor		 cursor_colour;
static int		 width;
static int		 height;
static int		 loff;
static int		 cw;
static int		 ch;
static int		 alive	       = 1;
static int		 buf_len       = 0;
static int		 buf_lines;
static sq_buffer*	 text_buffer   = NULL;
static int		 cursor	       = 0;
static int		 scroll_offset = 0;
static char		 cur_filename[FILENAME_MAX];
static char		 tab_buffer[32];
static char		 display_buffer[MAX_BUF_LEN + 1];


static void update_display_buffer(void);
static void init_x(void);
static void count_lines(void);
static void draw_background(void);
static void draw_cursor(int x, int y);
static void draw_text(XftColor *colour, int x, int y, const char *text, int len);
static void draw_buffer(void);
static void goto_line(int lineno);
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
static void handle_meta(int state, KeySym key_sym);
static void insert_text(char *text, int len);
static void delete_text(int count);
static void event_loop(void);

static void update_display_buffer(void) {
  if (!text_buffer) return;
  // assuming display_buffer is large enough
  buffer_get_all(text_buffer, display_buffer);
}

void
init_x(void)
{
  XGlyphInfo extents;

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
  XftTextExtentsUtf8(dpy, font, (FcChar8 *)"W", 1, &extents);
  cw = extents.xOff;
  ch = font->ascent + font->descent;
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), FG_COLOUR, &fg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), BG_COLOUR, &bg_colour);
  XftColorAllocName(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), CURSOR_COLOUR, &cursor_colour);
}

void
count_lines(void)
{
  int i, digits = 0;
  buf_len = buffer_length(text_buffer);

  loff = cw;
  buf_lines = 1;
  for (i = 0; i < buf_len; ++i) {
    if (buffer_at(text_buffer, i) == '\n')
      ++buf_lines;
  }
  for (i = buf_lines; i != 0; i /= 10)
    ++digits;
  loff = cw * (digits + 1);
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
  buf_len = buffer_length(text_buffer);
  update_display_buffer();
  int cursor_ypos = 0, cursor_xpos, start = 0, i, len, y, lineno_len;
  char lineno_str[32];

  draw_background();
  for (i = 0; i <= buf_len; ++i) {
  
    if (i == buf_len || display_buffer[i] == '\n') {
      len = i - start;
      if ((y = ch + (cursor_ypos - scroll_offset) * ch) >= 0 && y <= height) {
        lineno_len = snprintf(lineno_str, 32, "%d ", cursor_ypos);
        draw_text(&fg_colour, 0, y, tab_buffer, loff - (cw * lineno_len));
        draw_text(&fg_colour, loff - (cw * lineno_len), y, lineno_str, lineno_len);
        if (cursor >= start && cursor <= i) {
          cursor_xpos = cursor - start;
          draw_text(&fg_colour, loff, y, display_buffer + start, cursor_xpos);
          draw_cursor(loff + cursor_xpos * cw, y);
          if (cursor_xpos < len) {
            draw_text(&bg_colour, loff + cursor_xpos * cw, y, display_buffer + start + cursor_xpos, 1);
            draw_text(&fg_colour, loff + (cursor_xpos + 1) * cw, y, display_buffer + start + cursor_xpos + 1, len - cursor_xpos - 1);
          }
        } else {
          draw_text(&fg_colour, loff, y, display_buffer + start, len);
        }
      }
      start = i + 1;
      ++cursor_ypos;
    }
  }
}

void
goto_line(int lineno)
{
  int i, line = 1;
  buf_len = buffer_length(text_buffer);
  for (i = 0; i < buf_len && line != lineno; ++i) {
    if (buffer_at(text_buffer, i) == '\n')
      ++line;
  }
  cursor = i;
}

void
goto_end_of_line(void)
{
  buf_len = buffer_length(text_buffer);
  while (cursor < buf_len && buffer_at(text_buffer, cursor) != '\n')
    ++cursor;
}

void
goto_start_of_line(void)
{
  while (cursor > 0 && buffer_at(text_buffer, cursor - 1) != '\n')
    --cursor;
}

void
ensure_cursor_visible(void)
{
  int i, cursor_ypos = 0;

  for (i = 0; i < cursor; ++i) {
    if (buffer_at(text_buffer, i) == '\n')
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

  for (i = cursor; i > 0 && buffer_at(text_buffer, i - 1) != '\n'; --i)
    ++col_cur;
  if (i-- == 0)
    return;
  for (; i > 0 && buffer_at(text_buffer, i - 1) != '\n'; --i)
    ++col_prev;
  cursor = i + MIN(col_cur, col_prev);
  ensure_cursor_visible();
}

void
next_line(void)
{
  int i, col_cur = 0, col_next = 0;
  buf_len = buffer_length(text_buffer);

  for (i = cursor; i > 0 && buffer_at(text_buffer, i - 1) != '\n'; --i)
    ++col_cur;
  for (i = cursor; i < buf_len && buffer_at(text_buffer, i) != '\n'; ++i)
    ;
  if (i++ == buf_len)
    return;
  cursor = i;
  for (; i < buf_len && buffer_at(text_buffer, i) != '\n'; ++i)
    ++col_next;
  cursor += MIN(col_cur, col_next);
  ensure_cursor_visible();
}

void
forward_char(void)
{
  buf_len = buffer_length(text_buffer);
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
  text_buffer = buffer_create_from_file(cur_filename);
  if (!text_buffer) {
    text_buffer = buffer_create();
    if (!text_buffer) {
      fprintf(stderr, "could not create buffer\n");
      exit(1);
    }
  }
  cursor = 0;
  scroll_offset = 0;
}

void
save_to_file(void)
{
  FILE *fp;

  if ((fp = fopen(cur_filename, "w")) == NULL) {
    fprintf(stderr, "could not open \"%s\"\n", cur_filename);
    exit(1);
  }
  update_display_buffer();
  if (fwrite(display_buffer, 1, buf_len, fp) <= 0) {
    fprintf(stderr, "could not write to \"%s\"\n", cur_filename);
    exit(1);
  }
  fclose(fp);
}

void
handle_ctrl(KeySym key_sym)
{
  int start, len, i;
  int prev_len = buffer_length(text_buffer);
  int prev_cursor = cursor;


  switch (key_sym) {
  case XK_q:
    alive = 0;
    break;
  case XK_s:
    save_to_file();
    break;
  case XK_z:
    if (buffer_undo(text_buffer)) {
      buf_len = buffer_length(text_buffer);
      cursor = prev_cursor - (prev_len - buf_len);
      // clamp ?
      if (cursor < 0) cursor = 0;
      if (cursor > buf_len) cursor = buf_len;
    }
    break;
  case XK_y:
    if (buffer_redo(text_buffer)) {
      buf_len = buffer_length(text_buffer);
      cursor = prev_cursor - (prev_len - buf_len);
      if (cursor < 0) cursor = 0;
      if (cursor > buf_len) cursor = buf_len;
    }
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
      delete_text(1);
    }
    break;
  case XK_k:
    if (cursor < buf_len && buffer_at(text_buffer, cursor) == '\n') {
      ++cursor;
      delete_text(1);
    } else {
      start = cursor;
      while (cursor < buf_len && buffer_at(text_buffer, cursor) != '\n')
        ++cursor;
      len = cursor - start;
      delete_text(len);
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
handle_meta(int state, KeySym key_sym)
{
  int start, len, i;

  if (state & ShiftMask) {
    if (key_sym == XK_comma)
      goto_line(1);
    else if (key_sym == XK_period)
      goto_line(buf_lines);
    ensure_cursor_visible();
  }
  switch (key_sym) {
  case XK_f:
    while (cursor < buf_len && isspace(buffer_at(text_buffer, cursor)))
      ++cursor;
    while (cursor < buf_len && !isspace(buffer_at(text_buffer, cursor)))
      ++cursor;
    break;
  case XK_b:
    while (cursor > 0 && isspace(buffer_at(text_buffer, cursor - 1)))
      --cursor;
    while (cursor > 0 && !isspace(buffer_at(text_buffer, cursor - 1)))
      --cursor;
    break;
  case XK_d:
    start = cursor;
    while (cursor < buf_len && isspace(buffer_at(text_buffer, cursor)))
      ++cursor;
    while (cursor < buf_len && !isspace(buffer_at(text_buffer, cursor)))
      ++cursor;
    len = cursor - start;
    delete_text(len);
    break;
  case XK_v:
    for (i = 0; i < height / ch; ++i)
      prev_line();
    break;
  case XK_BackSpace:
    start = cursor;
    while (cursor > 0 && isspace(buffer_at(text_buffer, cursor - 1)))
      --cursor;
    while (cursor > 0 && !isspace(buffer_at(text_buffer, cursor - 1)))
      --cursor;
    len = start - cursor;
    cursor = start;
    delete_text(len);
    break;
  default:
    break;
  }
}

void
insert_text(char *text, int len)
{
  if (!text_buffer || !text || len <= 0) return;
  buffer_insert(text_buffer, cursor, text, len);
  cursor += len;
  count_lines();
  ensure_cursor_visible();
}

void
delete_text(int count)
{
  if (!text_buffer || count <= 0 || cursor < count) return;
  buffer_delete(text_buffer, cursor - count, count);
  cursor -= count;
  count_lines();
  ensure_cursor_visible();
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
      len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
      // key_sym = XLookupKeysym(key_event, 0);
      
      // i dont know how to fix it honestly, for example it registers my 'q' as 'й' no matter what
      if (len == 1 && buf[0] >= 'a' && buf[0] <= 'z')
	key_sym = (KeySym)buf[0];
      
      
      if (key_event->state & ControlMask) {
        handle_ctrl(key_sym);
      } else if (key_event->state & (Mod1Mask /*| ALT_MASK */)) {
        handle_meta(key_event->state, key_sym);
      } else {
        len = XLookupString(key_event, buf, sizeof(buf), &key_sym, NULL);
        switch (key_sym) {
        case XK_Return:
          insert_text("\n", 1);
          ensure_cursor_visible();
          break;
        case XK_Tab:
          insert_text(tab_buffer, TABSTOP);
          break;
        case XK_BackSpace:
          if (cursor > 0)
            delete_text(1);
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
  count_lines();
  event_loop();
  return 0;
}
