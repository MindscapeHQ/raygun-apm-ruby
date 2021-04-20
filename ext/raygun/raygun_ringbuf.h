#ifndef RAYGUN_RINGBUF_H
#define RAYGUN_RINGBUF_H

#include "raygun_coercion.h"
#include "bipbuffer.h"

extern VALUE rb_mRaygunApm;
extern VALUE rb_cRaygunRingbuf;

// Ruby interface that wraps https://github.com/willemt/bipbuffer - currently not used, but useful to keep around for more detalied sink tests if ever needed

void _init_raygun_ringbuf();
typedef struct _rg_ringbuf_t
{
    bipbuf_t *bipbuf;
} rg_ringbuf_t;

// Coerces a Ruby object -> a ring buffer C struct
extern const rb_data_type_t rb_rg_ringbuf_type;
#define rb_rg_get_ringbuf(obj) \
    rg_ringbuf_t *ringbuf = NULL; \
    TypedData_Get_Struct(obj, rg_ringbuf_t, &rb_rg_ringbuf_type, ringbuf); \
    if (!ringbuf) rb_raise(rb_eRaygunFatal, "Could not initialize ringbuf"); \

#endif
