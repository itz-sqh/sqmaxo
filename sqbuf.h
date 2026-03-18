/*
  sqbuf.h - v0.67

  Persistent treap - based text buffer with versioning support
  All operations preserve previous versions making it easy implementing undo/redo
  
  To use this library:
  #define SQBUF_IMPLEMENTATION
  #include "sqbuf.h"

  public api:

  buffer_create:
    sq_buffer* buffer_create(void);
    Creates a new empty buffer.

  buffer_create_from_file:
    sq_buffer* buffer_create_from_file(const char* filename);
    Creates a buffer initialized with contents of a file.

  buffer_free:
    void buffer_free(sq_buffer* buf);
    Frees buffer and all its versions.

  buffer_insert:
    void buffer_insert(sq_buffer* buf, int idx, const char* s, int len);
    Inserts text at specified position. Creates a new version.
    O(log(n) + len)

  buffer_delete:
    void buffer_delete(sq_buffer* buf, int idx, int count);
    Deletes text from specified position. Creates a new version.
    O(log(n) + count)

  buffer_length:
    size_t buffer_length(sq_buffer* buf);
    Returns current buffer length.
    O(1)

  buffer_at:
    char buffer_at(sq_buffer* buf, int idx);
    Returns character at specified position.
    O(log(n))

  buffer_get_all:
    void buffer_get_all(sq_buffer* buf, char* out);
    Copies entire buffer contents to output string (null-terminated).
    O(n)

  buffer_get:
    void buffer_get(sq_buffer* buf, int idx, int count, char* out);
    Copies substring from buffer to output string (null-terminated).
    O(n)

  buffer_undo:
    int buffer_undo(sq_buffer* buf);
    Undoes last operation. Returns 1 if undo succeeded.
    O(1)

  buffer_redo:
    int buffer_redo(sq_buffer* buf);
    Redoes previously undone operation. Returns 1 if redo succeeded.
    O(1)

    

  

  

*/




#ifndef INCLUDE_SQBUF_H
#define INCLUDE_SQBUF_H



#define SQBUF_VERSION 1


#ifndef SQBUFDEF
#ifdef SQBUF_STATIC
#define SQBUFDEF static
#else
#define SQBUFDEF extern
#endif
#endif


#ifndef SQBUF_NO_SHORT_NAMES
#define buffer_create sq_buffer_create
#define buffer_create_from_file sq_buffer_create_from_file
#define buffer_free sq_buffer_free
#define buffer_insert sq_buffer_insert
#define buffer_delete sq_buffer_delete
#define buffer_length sq_buffer_length
#define buffer_at sq_buffer_at
#define buffer_get_all sq_buffer_get_all
#define buffer_get sq_buffer_get
#define buffer_undo sq_buffer_undo
#define buffer_redo sq_buffer_redo
#endif

    

typedef struct sq__node sq__node;

#include <stddef.h>

struct sq__node {
  size_t subtree_size;
  sq__node *left, *right;
  int refcnt;
  char ch;
};

typedef sq__node* sq__treap;

#define SQBUF_MAX_VERSIONS 1024
typedef struct {
  sq__treap versions[SQBUF_MAX_VERSIONS];
  int cur_version;
  int version_count;
} sq_buffer;

SQBUFDEF sq_buffer* sq_buffer_create(void);
SQBUFDEF sq_buffer* sq_buffer_create_from_file(const char* filename);
SQBUFDEF void sq_buffer_free(sq_buffer* buf);

SQBUFDEF void sq_buffer_insert(sq_buffer* buf, int idx, const char* s, int len);
SQBUFDEF void sq_buffer_delete(sq_buffer* buf, int idx, int count);

SQBUFDEF size_t sq_buffer_length(sq_buffer* buf);
SQBUFDEF char sq_buffer_at(sq_buffer* buf, int idx);

SQBUFDEF void sq_buffer_get_all(sq_buffer* buf, char* out);
SQBUFDEF void sq_buffer_get(sq_buffer* buf, int idx, int count, char* out);

SQBUFDEF int sq_buffer_undo(sq_buffer* buf);
SQBUFDEF int sq_buffer_redo(sq_buffer* buf); 


#endif // INCLUDE_SQBUF_H



#ifdef SQBUF_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void sq__node_ref(sq__node* nd) {
  if (nd) nd->refcnt++;
}

static void sq__node_deref(sq__node* nd) {
  if (!nd || --nd->refcnt > 0) return;
  sq__node_deref(nd->left);
  sq__node_deref(nd->right);
  free(nd);
}

static sq__node* sq__create_node(char ch) {
  sq__node* nd = (sq__node*)malloc(sizeof(sq__node));
  if (!nd) return NULL;
  *nd = (sq__node) {
    .ch = ch,
    .subtree_size = 1,
    .refcnt = 1,
    .left = NULL,
    .right = NULL
  };
  return nd;
}

static size_t sq__treap_subtree_size(sq__treap t) {
  return t != NULL ? t->subtree_size : 0;
}

static void sq__node_update(sq__node* t) {
  if (t)
    t->subtree_size = 1 +
      sq__treap_subtree_size(t->left) +
      sq__treap_subtree_size(t->right);
}

static sq__node* sq__node_copy(sq__node* src) {
  if (!src) return NULL;
  sq__node* dst = (sq__node*)malloc(sizeof(sq__node));
  *dst = *src;
  dst->refcnt = 1;
  sq__node_ref(dst->left);
  sq__node_ref(dst->right);
  return dst;
}


static void sq__treap_split(sq__treap t, int pos, sq__treap* l, sq__treap* r) {
  if (t == NULL) {
    *l = *r = NULL;
    return;
  }
  t = sq__node_copy(t);
  int left_size = sq__treap_subtree_size(t->left);
  if (pos <= left_size) {
    sq__treap_split(t->left, pos, l, &(t->left));
    *r = t;
  }
  else {
    sq__treap_split(t->right, pos - left_size - 1, &(t->right), r);
    *l = t;
  }
  sq__node_update(t);
}

static void sq__treap_merge(sq__treap* t, sq__treap l, sq__treap r) {
  if (!l || !r) {
    *t = (l == NULL ? r : l);
    return;
  }
  int left_size = sq__treap_subtree_size(l);
  int right_size = sq__treap_subtree_size(r);
  if (rand() % (left_size + right_size) < left_size) {
    l = sq__node_copy(l);
    sq__treap_merge(&(l->right), l->right, r);
    *t = l;
  }
  else {
    r = sq__node_copy(r);
    sq__treap_merge(&(r->left), l, r->left);
    *t = r;
  }
  sq__node_update(*t);
}

static void sq__treap_build(sq__treap* t, const char* s, int l, int r) {
  if (l > r) return;
  int mid = (l + r) / 2;
  *t = sq__create_node(s[mid]);
  sq__treap_build(&((*t)->left), s, l, mid - 1);
  sq__treap_build(&((*t)->right), s, mid + 1, r);
  sq__node_update(*t);
}

static void sq__treap_insert(sq__treap* t, int pos, const char* s, int len)
{
  sq__treap l, m, r;
  sq__treap_split(*t, pos, &l, &r);
  sq__treap_build(&m, s, 0, len - 1);
  sq__treap_merge(t, l, m);
  sq__treap_merge(t, *t, r);
}

static void sq__treap_delete(sq__treap* t, int pos, int count)
{
  sq__treap l, m, r;
  sq__treap_split(*t, pos, &l, &m);
  sq__treap_split(m, count, &m, &r);
  sq__node_deref(m);
  sq__treap_merge(t, l, r);
}

static char sq__treap_at(sq__treap t, int idx) {
  if (!t || idx < 0) return '\0';
  
  int left_size = sq__treap_subtree_size(t->left);
  if (idx < left_size)
    return sq__treap_at(t->left, idx);
  else if (idx == left_size)
    return t->ch;
  else
    return sq__treap_at(t->right, idx - left_size - 1);
}

static void sq__treap_get_string(sq__treap t, char* out, int* curlen)
{
  if (t == NULL) return;
  sq__treap_get_string(t->left, out, curlen);
  out[(*curlen)++] = t->ch;
  sq__treap_get_string(t->right, out, curlen);
}

static void sq__treap_get_substring(sq__treap t, int idx, int count, char* out) {
  sq__treap l, m, r;
  sq__treap_split(t, idx, &l, &m);
  sq__treap_split(m, count, &m, &r);
  int curlen = 0;
  sq__treap_get_string(m, out, &curlen);
  out[curlen] = '\0';
  sq__node_deref(l);
  sq__node_deref(m);
  sq__node_deref(r);
  return;  
}


SQBUFDEF sq_buffer* sq_buffer_create(void) {
  sq_buffer* buf = (sq_buffer*)malloc(sizeof(sq_buffer));
  if (!buf) return NULL;

  for (int i = 0; i < SQBUF_MAX_VERSIONS; i++)
    buf->versions[i] = NULL;
  buf->cur_version = 0;
  buf->version_count = 1;
  return buf;    
}

SQBUFDEF sq_buffer* sq_buffer_create_from_file(const char* filename) {
  if (!filename) return NULL;
  FILE* f = fopen(filename, "rb");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* data = (char*)malloc(file_size + 1);
  if (!data) {
    fclose(f);
    return NULL;
  }

  fread(data, 1, file_size, f);
  data[file_size] = '\0';

  sq_buffer* buf = sq_buffer_create();
  if (!buf) {
    free(data);
    return NULL;
  }

  sq_buffer_insert(buf, 0, data, file_size);
  free(data);
  return buf;
}

SQBUFDEF void sq_buffer_free(sq_buffer* buf) {
  if (!buf) return;
  for (int i = 0; i < buf->version_count && i < SQBUF_MAX_VERSIONS; i++) {
    sq__node_deref(buf->versions[i]);
  }
  free(buf);
}


SQBUFDEF void sq_buffer_insert(sq_buffer* buf, int idx, const char* s, int len) {
  if (!buf || !s || idx < 0 || len <= 0) return;
  if (buf->cur_version < buf->version_count - 1) {
    for (int i = buf->cur_version + 1; i < buf->version_count; i++) {
      sq__node_deref(buf->versions[i]);
      buf->versions[i] = NULL;
    }
    buf->version_count = buf->cur_version + 1;
  }
  sq__treap nver = sq__node_copy(buf->versions[buf->cur_version]);
  sq__treap_insert(&nver, idx, s, len);
  buf->versions[++buf->cur_version] = nver;
  buf->version_count = buf->cur_version + 1;
}

SQBUFDEF void sq_buffer_delete(sq_buffer* buf, int idx, int count) {
  if (!buf || idx < 0 || count < 0) return;
  if (buf->cur_version < buf->version_count - 1) {
    for (int i = buf->cur_version + 1; i < buf->version_count; i++) {
      sq__node_deref(buf->versions[i]);
      buf->versions[i] = NULL;
    }
    buf->version_count = buf->cur_version + 1;
  }
  sq__treap nver = sq__node_copy(buf->versions[buf->cur_version]);
  sq__treap_delete(&nver, idx, count);
  buf->versions[++buf->cur_version] = nver;
  buf->version_count = buf->cur_version + 1;
}
SQBUFDEF size_t sq_buffer_length(sq_buffer* buf) {
  if (!buf) return 0;
  return sq__treap_subtree_size(buf->versions[buf->cur_version]);
}
SQBUFDEF char sq_buffer_at(sq_buffer* buf, int idx) {
  if (!buf) return '\0';
  sq__treap cur = buf->versions[buf->cur_version];
  return sq__treap_at(cur, idx);
}
SQBUFDEF void sq_buffer_get_all(sq_buffer* buf, char* out) {
  if (!buf || !out) return;
  int curlen = 0;
  sq__treap_get_string(buf->versions[buf->cur_version], out, &curlen);
  out[curlen] = '\0';
}
SQBUFDEF void sq_buffer_get(sq_buffer* buf, int idx, int count, char* out) {
  if (!buf || !out || count <= 0 || idx < 0) return;
  sq__treap_get_substring(buf->versions[buf->cur_version], idx, count, out);
}

SQBUFDEF int sq_buffer_undo(sq_buffer* buf) {
  if (!buf || buf->cur_version <= 1) return 0;
  buf->cur_version--;
  return 1;
}

SQBUFDEF int sq_buffer_redo(sq_buffer* buf) {
  if (!buf || buf->cur_version >= buf->version_count - 1) return 0;
  buf->cur_version++;
  return 1;
}



#endif // SQBUF_IMPLEMENTATION
