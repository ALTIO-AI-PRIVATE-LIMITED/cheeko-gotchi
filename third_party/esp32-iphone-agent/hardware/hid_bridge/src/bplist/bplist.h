// Minimal binary plist ("bplist00") reader and writer: only the types AirPlay uses
// (dict, array, ASCII/UTF-16 string, data, unsigned int, real, bool).
// The reader works in place on the input buffer (no allocation); the writer builds a
// small tree and serializes it into one malloc'ed buffer.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { BP_NONE, BP_BOOL, BP_UINT, BP_REAL, BP_DATA, BP_STRING, BP_ARRAY, BP_DICT };

// ---- reader ----
typedef struct {
  const uint8_t *buf;
  size_t len;
  uint8_t off_size, ref_size;
  uint64_t count, top, table;
} bp_reader;

int bp_open(bp_reader *r, const void *data, size_t len);  // root ref, or -1 if not a valid bplist
int bp_type(const bp_reader *r, int ref);
int bp_count(const bp_reader *r, int ref);                   // items in an array or dict
int bp_array_get(const bp_reader *r, int ref, int i);
int bp_dict_get(const bp_reader *r, int ref, const char *key);  // value ref, -1 if absent
bool bp_get_uint(const bp_reader *r, int ref, uint64_t *out);
bool bp_get_bool(const bp_reader *r, int ref, bool *out);
bool bp_get_real(const bp_reader *r, int ref, double *out);
bool bp_get_data(const bp_reader *r, int ref, const uint8_t **out, size_t *len);
bool bp_get_string(const bp_reader *r, int ref, char *out, size_t cap);  // UTF-8, NUL-terminated

// ---- writer ----
typedef struct bp_wnode bp_wnode;
typedef struct {
  bp_wnode *nodes;
  int count, cap;
  bool oom;
} bp_writer;

void bp_writer_init(bp_writer *w);
void bp_writer_free(bp_writer *w);
int bp_w_dict(bp_writer *w);
int bp_w_array(bp_writer *w);
int bp_w_uint(bp_writer *w, uint64_t v);
int bp_w_real(bp_writer *w, double v);
int bp_w_bool(bp_writer *w, bool v);
int bp_w_string(bp_writer *w, const char *s);  // ASCII
int bp_w_data(bp_writer *w, const void *p, size_t n);
void bp_w_set(bp_writer *w, int dict, const char *key, int value);
void bp_w_append(bp_writer *w, int array, int value);
// Serializes the tree rooted at node 0; returns a malloc'ed buffer (caller frees) or NULL.
uint8_t *bp_w_finish(bp_writer *w, size_t *len);

#ifdef __cplusplus
}
#endif
