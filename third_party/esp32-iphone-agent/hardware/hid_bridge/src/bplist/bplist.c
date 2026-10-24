// Minimal binary plist codec, see bplist.h.
#include "bplist.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- reader

static uint64_t be(const uint8_t *p, int n) {
  uint64_t v = 0;
  while (n--) v = (v << 8) | *p++;
  return v;
}

int bp_open(bp_reader *r, const void *data, size_t len) {
  memset(r, 0, sizeof *r);
  const uint8_t *b = data;
  if (!b || len < 8 + 32 || memcmp(b, "bplist00", 8)) return -1;
  const uint8_t *t = b + len - 32;
  r->buf = b;
  r->len = len;
  r->off_size = t[6];
  r->ref_size = t[7];
  r->count = be(t + 8, 8);
  r->top = be(t + 16, 8);
  r->table = be(t + 24, 8);
  if (r->off_size < 1 || r->off_size > 8 || r->ref_size < 1 || r->ref_size > 8) return -1;
  if (r->top >= r->count || r->table < 8 || r->count > len) return -1;
  if (r->table + r->count * r->off_size > len - 32) return -1;
  return (int)r->top;
}

// Offset of object `ref`, or 0 if invalid.
static size_t obj_at(const bp_reader *r, int ref) {
  if (!r->buf || ref < 0 || (uint64_t)ref >= r->count) return 0;
  uint64_t off = be(r->buf + r->table + (uint64_t)ref * r->off_size, r->off_size);
  return (off >= 8 && off < r->table) ? (size_t)off : 0;
}

// Reads the marker at `pos`: returns its high nibble; *n = item count/length, *body = first byte after the header.
static int header(const bp_reader *r, size_t pos, uint64_t *n, size_t *body) {
  uint8_t m = r->buf[pos];
  *n = m & 0x0f;
  *body = pos + 1;
  if ((m >> 4) != 0 && (m >> 4) != 1 && (m >> 4) != 2 && *n == 0x0f) {  // length follows as an int object
    if (*body >= r->table || (r->buf[*body] >> 4) != 1) return -1;
    int sz = 1 << (r->buf[*body] & 0x0f);
    if (sz > 8 || *body + 1 + sz > r->table) return -1;
    *n = be(r->buf + *body + 1, sz);
    *body += 1 + sz;
  }
  return m >> 4;
}

int bp_type(const bp_reader *r, int ref) {
  size_t pos = obj_at(r, ref), body;
  uint64_t n;
  if (!pos) return BP_NONE;
  switch (header(r, pos, &n, &body)) {
    case 0x0: return (n == 8 || n == 9) ? BP_BOOL : BP_NONE;
    case 0x1: return BP_UINT;
    case 0x2: return BP_REAL;
    case 0x4: return BP_DATA;
    case 0x5: case 0x6: return BP_STRING;
    case 0xA: return BP_ARRAY;
    case 0xD: return BP_DICT;
    default: return BP_NONE;
  }
}

// Body of a container with `want` marker, checking that all refs fit in the object area.
static bool container(const bp_reader *r, int ref, int want, uint64_t *n, size_t *body) {
  size_t pos = obj_at(r, ref);
  if (!pos || header(r, pos, n, body) != want) return false;
  uint64_t refs = want == 0xD ? *n * 2 : *n;
  return *body + refs * r->ref_size <= r->table;
}

int bp_count(const bp_reader *r, int ref) {
  uint64_t n;
  size_t body;
  if (container(r, ref, 0xA, &n, &body) || container(r, ref, 0xD, &n, &body)) return (int)n;
  return 0;
}

int bp_array_get(const bp_reader *r, int ref, int i) {
  uint64_t n;
  size_t body;
  if (!container(r, ref, 0xA, &n, &body) || i < 0 || (uint64_t)i >= n) return -1;
  return (int)be(r->buf + body + (size_t)i * r->ref_size, r->ref_size);
}

int bp_dict_get(const bp_reader *r, int ref, const char *key) {
  uint64_t n;
  size_t body;
  char k[128];
  if (!container(r, ref, 0xD, &n, &body)) return -1;
  for (uint64_t i = 0; i < n; i++) {
    int kr = (int)be(r->buf + body + i * r->ref_size, r->ref_size);
    if (bp_get_string(r, kr, k, sizeof k) && !strcmp(k, key))
      return (int)be(r->buf + body + (n + i) * r->ref_size, r->ref_size);
  }
  return -1;
}

bool bp_get_uint(const bp_reader *r, int ref, uint64_t *out) {
  size_t pos = obj_at(r, ref);
  if (!pos || (r->buf[pos] >> 4) != 1) return false;
  int sz = 1 << (r->buf[pos] & 0x0f);
  if (sz > 16 || pos + 1 + sz > r->table) return false;
  *out = sz == 16 ? be(r->buf + pos + 9, 8) : be(r->buf + pos + 1, sz);  // 16-byte ints: low 8 bytes
  return true;
}

bool bp_get_bool(const bp_reader *r, int ref, bool *out) {
  size_t pos = obj_at(r, ref);
  if (!pos || (r->buf[pos] != 0x08 && r->buf[pos] != 0x09)) return false;
  *out = r->buf[pos] == 0x09;
  return true;
}

bool bp_get_real(const bp_reader *r, int ref, double *out) {
  size_t pos = obj_at(r, ref);
  if (!pos || (r->buf[pos] >> 4) != 2) return false;
  int sz = 1 << (r->buf[pos] & 0x0f);
  if ((sz != 4 && sz != 8) || pos + 1 + sz > r->table) return false;
  uint64_t bits = be(r->buf + pos + 1, sz);
  if (sz == 4) {
    uint32_t b32 = (uint32_t)bits;
    float f;
    memcpy(&f, &b32, 4);
    *out = f;
  } else {
    memcpy(out, &bits, 8);
  }
  return true;
}

bool bp_get_data(const bp_reader *r, int ref, const uint8_t **out, size_t *len) {
  size_t pos = obj_at(r, ref), body;
  uint64_t n;
  if (!pos || header(r, pos, &n, &body) != 0x4 || body + n > r->table) return false;
  *out = r->buf + body;
  *len = (size_t)n;
  return true;
}

static size_t put_utf8(char *o, uint32_t c) {
  if (c < 0x80) { o[0] = (char)c; return 1; }
  if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
  if (c < 0x10000) {
    o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F));
    return 3;
  }
  o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | ((c >> 12) & 0x3F));
  o[2] = (char)(0x80 | ((c >> 6) & 0x3F)); o[3] = (char)(0x80 | (c & 0x3F));
  return 4;
}

bool bp_get_string(const bp_reader *r, int ref, char *out, size_t cap) {
  size_t pos = obj_at(r, ref), body, o = 0;
  uint64_t n;
  if (!pos || !cap) return false;
  int kind = header(r, pos, &n, &body);
  if (kind == 0x5) {
    if (body + n > r->table || n >= cap) return false;
    memcpy(out, r->buf + body, (size_t)n);
    out[n] = 0;
    return true;
  }
  if (kind != 0x6 || body + n * 2 > r->table) return false;
  for (uint64_t i = 0; i < n; i++) {
    uint32_t c = (uint32_t)be(r->buf + body + i * 2, 2);
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {  // surrogate pair
      uint32_t lo = (uint32_t)be(r->buf + body + (i + 1) * 2, 2);
      if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
    }
    if (o + 4 >= cap) return false;
    o += put_utf8(out + o, c);
  }
  out[o] = 0;
  return true;
}

// ---------------------------------------------------------------- writer

struct bp_wnode {
  int type;
  uint64_t u;
  double d;
  uint8_t *bytes;   // string or data
  size_t len;
  int *keys, *vals;  // dict keys (string nodes) / array or dict values
  int n, cap;
};

void bp_writer_init(bp_writer *w) { memset(w, 0, sizeof *w); }

void bp_writer_free(bp_writer *w) {
  for (int i = 0; i < w->count; i++) {
    free(w->nodes[i].bytes);
    free(w->nodes[i].keys);
    free(w->nodes[i].vals);
  }
  free(w->nodes);
  memset(w, 0, sizeof *w);
}

static int add(bp_writer *w, int type) {
  if (w->count == w->cap) {
    int cap = w->cap ? w->cap * 2 : 16;
    bp_wnode *n = realloc(w->nodes, cap * sizeof *n);
    if (!n) { w->oom = true; return -1; }
    w->nodes = n;
    w->cap = cap;
  }
  memset(&w->nodes[w->count], 0, sizeof(bp_wnode));
  w->nodes[w->count].type = type;
  return w->count++;
}

int bp_w_dict(bp_writer *w) { return add(w, BP_DICT); }
int bp_w_array(bp_writer *w) { return add(w, BP_ARRAY); }

int bp_w_uint(bp_writer *w, uint64_t v) {
  int i = add(w, BP_UINT);
  if (i >= 0) w->nodes[i].u = v;
  return i;
}

int bp_w_real(bp_writer *w, double v) {
  int i = add(w, BP_REAL);
  if (i >= 0) w->nodes[i].d = v;
  return i;
}

int bp_w_bool(bp_writer *w, bool v) {
  int i = add(w, BP_BOOL);
  if (i >= 0) w->nodes[i].u = v;
  return i;
}

static int bytes_node(bp_writer *w, int type, const void *p, size_t n) {
  int i = add(w, type);
  if (i < 0) return -1;
  w->nodes[i].bytes = malloc(n ? n : 1);
  if (!w->nodes[i].bytes) { w->oom = true; return -1; }
  if (n) memcpy(w->nodes[i].bytes, p, n);
  w->nodes[i].len = n;
  return i;
}

int bp_w_string(bp_writer *w, const char *s) { return bytes_node(w, BP_STRING, s, strlen(s)); }
int bp_w_data(bp_writer *w, const void *p, size_t n) { return bytes_node(w, BP_DATA, p, n); }

static void push(bp_writer *w, int parent, int key, int value) {
  if (parent < 0 || value < 0 || (w->nodes[parent].type == BP_DICT && key < 0)) { w->oom = true; return; }
  bp_wnode *p = &w->nodes[parent];
  if (p->n == p->cap) {
    int cap = p->cap ? p->cap * 2 : 8;
    int *k = realloc(p->keys, cap * sizeof(int)), *v;
    if (k) p->keys = k;
    v = realloc(p->vals, cap * sizeof(int));
    if (v) p->vals = v;
    if (!k || !v) { w->oom = true; return; }
    p->cap = cap;
  }
  p->keys[p->n] = key;
  p->vals[p->n++] = value;
}

void bp_w_set(bp_writer *w, int dict, const char *key, int value) {
  int k = bp_w_string(w, key);  // may realloc nodes: resolve parent after
  push(w, dict, k, value);
}

void bp_w_append(bp_writer *w, int array, int value) { push(w, array, 0, value); }

typedef struct { uint8_t *p; size_t len, cap; bool oom; } buf_t;

static void put(buf_t *b, const void *p, size_t n) {
  if (b->len + n > b->cap) {
    size_t cap = (b->cap ? b->cap * 2 : 256) + n;
    uint8_t *q = realloc(b->p, cap);
    if (!q) { b->oom = true; return; }
    b->p = q;
    b->cap = cap;
  }
  memcpy(b->p + b->len, p, n);
  b->len += n;
}

static void put_be(buf_t *b, uint64_t v, int n) {
  uint8_t t[8];
  for (int i = n - 1; i >= 0; i--, v >>= 8) t[i] = (uint8_t)v;
  put(b, t, n);
}

static int int_bytes(uint64_t v) { return v <= 0xFF ? 1 : v <= 0xFFFF ? 2 : v <= 0xFFFFFFFFu ? 4 : 8; }
static int log2_bytes(int n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }

static void put_marker(buf_t *b, int kind, uint64_t n) {
  if (n < 15) { uint8_t m = (uint8_t)(kind << 4 | n); put(b, &m, 1); return; }
  uint8_t m = (uint8_t)(kind << 4 | 0xF), im;
  int sz = int_bytes(n);
  put(b, &m, 1);
  im = (uint8_t)(0x10 | log2_bytes(sz));
  put(b, &im, 1);
  put_be(b, n, sz);
}

uint8_t *bp_w_finish(bp_writer *w, size_t *len) {
  if (w->oom || !w->count) return NULL;
  int ref = w->count < 256 ? 1 : 2;
  size_t *offs = malloc(w->count * sizeof *offs);
  buf_t b = {0};
  if (!offs) return NULL;
  put(&b, "bplist00", 8);
  for (int i = 0; i < w->count; i++) {
    bp_wnode *n = &w->nodes[i];
    offs[i] = b.len;
    uint8_t m;
    switch (n->type) {
      case BP_BOOL: m = n->u ? 0x09 : 0x08; put(&b, &m, 1); break;
      case BP_UINT: {
        int sz = int_bytes(n->u);
        m = (uint8_t)(0x10 | log2_bytes(sz));
        put(&b, &m, 1);
        put_be(&b, n->u, sz);
        break;
      }
      case BP_REAL: {
        uint64_t bits;
        memcpy(&bits, &n->d, 8);
        m = 0x23;
        put(&b, &m, 1);
        put_be(&b, bits, 8);
        break;
      }
      case BP_DATA: case BP_STRING:
        put_marker(&b, n->type == BP_DATA ? 0x4 : 0x5, n->len);
        put(&b, n->bytes, n->len);
        break;
      case BP_ARRAY: case BP_DICT:
        put_marker(&b, n->type == BP_ARRAY ? 0xA : 0xD, n->n);
        if (n->type == BP_DICT)
          for (int k = 0; k < n->n; k++) put_be(&b, n->keys[k], ref);
        for (int k = 0; k < n->n; k++) put_be(&b, n->vals[k], ref);
        break;
    }
  }
  size_t table = b.len;
  int off = int_bytes(table);
  for (int i = 0; i < w->count; i++) put_be(&b, offs[i], off);
  free(offs);
  uint8_t trailer[6] = {0};
  put(&b, trailer, 6);
  put_be(&b, off, 1);
  put_be(&b, ref, 1);
  put_be(&b, w->count, 8);
  put_be(&b, 0, 8);
  put_be(&b, table, 8);
  if (b.oom) { free(b.p); return NULL; }
  *len = b.len;
  return b.p;
}
