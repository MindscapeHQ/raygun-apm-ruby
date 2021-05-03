#ifndef RAYGUN_EXT_H
#define RAYGUN_EXT_H

// Header for the cruby API integration

#include "raygun_coercion.h"

extern VALUE rb_mRaygun;
extern VALUE rb_mRaygunApm;

#include "raygun_tracer.h"
#include "raygun_event.h"
#include "raygun_ringbuf.h"
#include "raygun_trace_context.h"

#endif
