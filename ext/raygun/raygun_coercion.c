#include "raygun_coercion.h"

#define 	BIGNUM_SIGN_BIT   ((VALUE)FL_USER1)
#define 	BIGNUM_SIGN(b)   ((RBASIC(b)->flags & BIGNUM_SIGN_BIT) != 0)
#define 	BIGNUM_POSITIVE_P(b)   BIGNUM_SIGN(b)
#define 	BIGNUM_NEGATIVE_P(b)   (!BIGNUM_SIGN(b))
// Coercion helpers

rb_encoding *rg_default_encoding,
  *rg_utf16le_encoding,
  *rg_utf16be_encoding,
  *rg_ascii_encoding,
  *rg_utf7_encoding,
  *rg_utf8_encoding,
  *rg_utf32le_encoding;

static inline void rb_rg_vt_coerce_string(rg_variable_info_t *var, VALUE name, VALUE obj, rb_encoding *enc, VALUE ecopts)
{
  int ecflags = ECONV_INVALID_REPLACE | ECONV_UNDEF_REPLACE;
  VALUE encoded;

  if (RSTRING_LEN(obj) == 0) {
    rb_rg_variable_info_init(var, name, RG_VT_EMPTYSTRING);
  } else {
    encoded = rb_str_encode(obj, rb_enc_from_encoding(enc), ecflags, ecopts);
    if (RSTRING_LEN(encoded) >= RG_MAX_STRING_SIZE) {
      rb_rg_variable_info_init(var, name, RG_VT_LARGESTRING);
      rb_rg_encode_largestring_raw(&var->as.t_largestring, encoded);
      var->length += sizeof(var->as.t_largestring.length) + var->as.t_largestring.length;
    } else {
      rb_rg_variable_info_init(var, name, RG_VT_STRING);
      rb_rg_encode_string_raw(&var->as.t_encoded_string, encoded);
      var->length += sizeof(var->as.t_encoded_string.length) + var->as.t_encoded_string.length;
    }
  }
}

static inline void rb_rg_vt_coerce_int32(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_INT32);
  var->as.t_int32 = rb_rg_encode_int32(obj);
  var->length += sizeof(var->as.t_int32);
}

static inline void rb_rg_vt_coerce_unsigned_int32(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_UNSIGNED_INT32);
  var->as.t_unsigned_int32 = rb_rg_encode_unsigned_int32(obj);
  var->length += sizeof(var->as.t_unsigned_int32);
}

static inline void rb_rg_vt_coerce_short(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_SHORT);
  var->as.t_short = rb_rg_encode_short(obj);
  var->length += sizeof(var->as.t_short);
}

static inline void rb_rg_vt_coerce_unsigned_short(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_UNSIGNED_SHORT);
  var->as.t_unsigned_short = rb_rg_encode_unsigned_short(obj);
  var->length += sizeof(var->as.t_unsigned_short);
}

static inline void rb_rg_vt_coerce_long(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_LONG);
  var->as.t_long = rb_rg_encode_long(obj);
  var->length += sizeof(var->as.t_long);
}

static inline void rb_rg_vt_coerce_unsigned_long(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_UNSIGNED_LONG);
  var->as.t_unsigned_long = rb_rg_encode_unsigned_long(obj);
  var->length += sizeof(var->as.t_unsigned_long);
}

static inline void rb_rg_vt_coerce_fixnum(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  // TODO: naive check, but also how ruby-core boundary checks
  long long val;
  val = rb_num2ll(obj);
  if (val >= 0 && val <= UINT16_MAX)
  {
    rb_rg_vt_coerce_unsigned_short(var, name, obj);
  } else if (val < 0 && val >= INT16_MIN)
  {
    rb_rg_vt_coerce_short(var, name, obj);
  } else if (val >= INT16_MAX && val <= UINT32_MAX)
  {
    rb_rg_vt_coerce_unsigned_int32(var, name, obj);
  } else if (val < INT16_MIN && val >= INT32_MIN)
  {
    rb_rg_vt_coerce_int32(var, name, obj);
  } else if (val > INT32_MAX) {
    rb_rg_vt_coerce_unsigned_long(var, name, obj);
  } else if (val < INT32_MIN) {
    rb_rg_vt_coerce_long(var, name, obj);
  } else
  {
    rb_raise(rb_eRaygunFatal, "Unhandled fixnum: %p", (void*)obj);
  }
}

static inline void rb_rg_vt_coerce_nil(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_NULLOBJECT);
}

static inline void rb_rg_vt_coerce_boolean(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rg_boolean_t t_bool = 0;
  rb_rg_variable_info_init(var, name, RG_VT_BOOLEAN);
  if (TYPE(obj) == T_TRUE){
    t_bool = 1;
  }
  var->as.t_boolean = t_bool;
}

static inline void rb_rg_vt_coerce_float(rg_variable_info_t *var, VALUE name, VALUE obj)
{
  rb_rg_variable_info_init(var, name, RG_VT_FLOAT);
  var->as.t_float = rb_rg_encode_float(obj);
}

rg_variable_info_t rb_rg_vt_coerce(VALUE name, VALUE obj, VALUE ecopts)
{
  rg_variable_info_t var;
  int status = 0;
  switch (TYPE(obj)) {
    case T_NIL:
      rb_rg_vt_coerce_nil(&var, name, obj);
      break;
    case T_FLOAT:
      rb_rg_vt_coerce_float(&var, name, obj);
      break;
    case T_TRUE:
    case T_FALSE:
      rb_rg_vt_coerce_boolean(&var, name, obj);
      break;
    case T_FIXNUM:
      rb_rg_vt_coerce_fixnum(&var, name, obj);
      break;
    case T_BIGNUM:
      if (BIGNUM_POSITIVE_P(obj))
      {
        //XXX ensure scientific notation do not blow up on string coercion
        unsigned long long i = (unsigned long long)rb_protect(rb_protect_rb_big2ull, obj, &status);
        if (UNLIKELY(status)) {
          rb_rg_vt_coerce_string(&var, name, rb_big2str(obj, 10), rg_default_encoding, ecopts);
          // Clearing error info to ignore the caught exception
          rb_set_errinfo(Qnil);
          return var;
        }
        if (i <= UINT32_MAX)
        {
          rb_rg_vt_coerce_unsigned_int32(&var, name, obj);
        }
        else if (i <= UINT64_MAX)
        {
          rb_rg_vt_coerce_unsigned_long(&var, name, obj);
        } else
        {
          rb_rg_vt_coerce_string(&var, name, obj, rg_default_encoding, ecopts);
        }
      } else
      {
        //XXX ensure scientific notation do not blow up on string coercion
        long long i = (long long)rb_protect(rb_protect_rb_big2ull, obj, &status);
        if (UNLIKELY(status)) {
          rb_rg_vt_coerce_string(&var, name, rb_big2str(obj, 10), rg_default_encoding, ecopts);
          // Clearing error info to ignore the caught exception
          rb_set_errinfo(Qnil);
          return var;
        }
        if (i >= INT32_MIN)
        {
          rb_rg_vt_coerce_int32(&var, name, obj);
        }
        else if (i >= INT64_MIN)
        {
          rb_rg_vt_coerce_long(&var, name, obj);
        } else
        {
          rb_rg_vt_coerce_string(&var, name, obj, rg_default_encoding, ecopts);
        }
      }
      break;
    case T_SYMBOL:
      rb_rg_vt_coerce_string(&var, name, rb_sym_to_s(obj), rg_default_encoding, ecopts);
      break;
    case T_STRING:
      if (ENCODING_IS_ASCII8BIT(obj))
      {
        // XXX TODO encode as byte array
      } else
      {
        rb_rg_vt_coerce_string(&var, name, obj, rg_default_encoding, ecopts);
        break;
      }
    default:
      // TODO: reinstate rb_obj_as_string when it's better understood what makes it segfault sometimes
      rb_rg_vt_coerce_string(&var, name, rb_class_name(CLASS_OF(obj)), rg_default_encoding, ecopts);
      break;
  }
  return var;
}

static const rb_encoding *rb_rg_enc_from_encoded_string(const rg_encoded_string_t * const string)
{
  switch((rg_string_encoding_t)string->encoding)
  {
    case RG_STRING_ENCODING_UTF_16LE:
      return rg_utf16le_encoding;
    case RG_STRING_ENCODING_UTF_16BE:
      return rg_utf16be_encoding;
    case RG_STRING_ENCODING_ASCII:
      return rg_ascii_encoding;
    case RG_STRING_ENCODING_UTF7:
      return rg_utf7_encoding;
    case RG_STRING_ENCODING_UTF8:
      return rg_utf8_encoding;
    case RG_STRING_ENCODING_UTF32LE:
      return rg_utf32le_encoding;
    case RG_STRING_ENCODING_NULL:
      return rg_default_encoding;
    default:
      return rg_default_encoding;
  }
}

void rb_rg_encode_string(rg_encoded_string_t *string, VALUE obj, VALUE ecopts)
{
  VALUE encoded;
  if (NIL_P(obj))
  {
    string->length = 0;
  } else
  {
    rg_long_t str_length;
    int ecflags = ECONV_INVALID_REPLACE | ECONV_UNDEF_REPLACE;
    encoded = rb_str_encode(obj, rb_enc_from_encoding(rb_rg_enc_from_encoded_string((const rg_encoded_string_t *)string)), ecflags, ecopts);
    str_length = RSTRING_LEN(encoded);
    if (str_length >= RG_MAX_STRING_SIZE) {
      string->length = RG_MAX_STRING_SIZE;
    } else {
      string->length = (rg_length_t)str_length;
    }
    memcpy(string->string, StringValuePtr(encoded), string->length);
    RB_GC_GUARD(encoded);
    RB_GC_GUARD(obj);
  }
}

// XXX abstract this better, used only for http VERB events for now
void rb_rg_encode_short_string(rg_encoded_short_string_t *string, VALUE obj, VALUE ecopts)
{
  VALUE encoded;
  if (NIL_P(obj))
  {
    string->length = 0;
  } else
  {
    rg_long_t str_length;
    int ecflags = ECONV_INVALID_REPLACE | ECONV_UNDEF_REPLACE;
    encoded = rb_str_encode(obj, rb_enc_from_encoding(rb_rg_enc_from_encoded_string((const rg_encoded_string_t *)string)), ecflags, ecopts);
    str_length = RSTRING_LEN(encoded);
    if (str_length >= RG_MAX_SHORT_STRING_SIZE) {
      string->length = RG_MAX_SHORT_STRING_SIZE;
    } else {
      string->length = (rg_length_t)str_length;
    }
    memcpy(string->string, StringValuePtr(encoded), string->length);
    RB_GC_GUARD(encoded);
    RB_GC_GUARD(obj);
  }
}

void rb_rg_encode_string_raw(rg_encoded_string_t *string, VALUE obj)
{
  if (NIL_P(obj))
  {
    string->length = 0;
  } else
  {
    string->length = (rg_length_t)RSTRING_LEN(obj);
    memcpy(string->string, StringValuePtr(obj), string->length);
    RB_GC_GUARD(obj);
  }
}

void rb_rg_encode_largestring_raw(rg_largestring_t *largestring, VALUE obj)
{
  // XXX add checks to truncate at proper codepoint boundary not potentially in the middle of surrogate pairs
  largestring->length = (rg_length_t)RG_MAX_STRING_SIZE;
  memcpy(largestring->string, StringValuePtr(obj), largestring->length);
  RB_GC_GUARD(obj);
}

// TODO: error state and return
void rb_rg_variable_info_init(rg_variable_info_t *var, VALUE obj, rg_variable_t type)
{
  var->type = type;
  if (LIKELY(NIL_P(obj))) {
    var->name_length = 0;
    var->length = sizeof(var->type) + sizeof(var->name_length);
  } else {
    VALUE name = obj;
    var->name_length = (rg_short_t)RSTRING_LEN(obj);
    if (var->name_length > RG_MAX_VARIABLE_NAME) {
      var->name_length = RG_MAX_VARIABLE_NAME;
      name = rb_str_substr(obj, 0, RG_MAX_VARIABLE_NAME);
    }
    var->length = sizeof(var->type) + sizeof(var->name_length) + var->name_length;
    memcpy(var->name, StringValuePtr(name), var->name_length);
    RB_GC_GUARD(name);
    RB_GC_GUARD(obj);
  }
}

void _init_raygun_coercion()
{
  rg_utf16le_encoding = rb_enc_from_index(rb_enc_find_index("UTF-16LE"));
  rg_utf16be_encoding = rb_enc_from_index(rb_enc_find_index("UTF-16BE"));
  rg_ascii_encoding = rb_ascii8bit_encoding();
  rg_utf7_encoding = rb_enc_from_index(rb_enc_find_index("UTF-7"));
  rg_utf8_encoding = rb_utf8_encoding();
  rg_utf32le_encoding = rb_enc_from_index(rb_enc_find_index("UTF-32LE"));
  rg_default_encoding = rg_utf16le_encoding;
}

VALUE rb_protect_rb_big2ull(VALUE arg)
{
    unsigned long long result = rb_big2ull(arg);
    return ULL2NUM(result);
}