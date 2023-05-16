#ifndef RAYGUN_COERCION_H
#define RAYGUN_COERCION_H

// Header for ruby -> Raygun type coercion

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Woverflow"
#include <ruby/ruby.h>
#include <ruby/encoding.h>
#include <ruby/debug.h>
#include <ruby/thread.h>
#include "vm_core.h"
#include <ruby/thread_native.h>
#pragma GCC diagnostic pop

#include "raygun.h"
#include "raygun_errors.h"

// References to supported Ruby encodings that map to the wire protocol for encoded strings
extern rb_encoding *rg_default_encoding,
  *rg_utf16le_encoding,
  *rg_utf16be_encoding,
  *rg_ascii_encoding,
  *rg_utf7_encoding,
  *rg_utf8_encoding,
  *rg_utf32le_encoding;

// Ruby -> native String type encoder helpers

void rb_rg_encode_string(rg_encoded_string_t *string, VALUE obj, VALUE ecopts);
void rb_rg_encode_short_string(rg_encoded_short_string_t *string, VALUE obj, VALUE ecopts);
void rb_rg_encode_string_raw(rg_encoded_string_t *string, VALUE obj);
void rb_rg_encode_largestring_raw(rg_largestring_t *largestring, VALUE obj);


// XXX revisit, looses precision (ruby floats are backed by double)
// Not really in scope as we don't encode any floats because argument encoding is turned off by default
static inline rg_float_t rb_rg_encode_float(VALUE obj)
{
  return (rg_float_t)RFLOAT_VALUE(obj);
}

// Numeric coercion helpers

static inline rg_int_t rb_rg_encode_int32(VALUE obj)
{
  return NUM2INT(obj);
}

static inline rg_unsigned_int_t rb_rg_encode_unsigned_int32(VALUE obj)
{
  return NUM2UINT(obj);
}

static inline rg_short_t rb_rg_encode_short(VALUE obj)
{
  return NUM2SHORT(obj);
}

static inline rg_unsigned_short_t rb_rg_encode_unsigned_short(VALUE obj)
{
  return NUM2USHORT(obj);
}

static inline rg_long_t rb_rg_encode_long(VALUE obj)
{
  return NUM2LL(obj);
}

static inline rg_unsigned_long_t rb_rg_encode_unsigned_long(VALUE obj)
{
  return NUM2ULL(obj);
}

// Coercion of Ruby to variable types (uses encoders above)

rg_variable_info_t rb_rg_vt_coerce(VALUE name, VALUE obj, VALUE ecopts);
void rb_rg_variable_info_init(rg_variable_info_t *var, VALUE obj, rg_variable_t type);

void _init_raygun_coercion();

//rb_protect wrappers
VALUE rb_protect_rb_big2ull(VALUE arg);

#endif
