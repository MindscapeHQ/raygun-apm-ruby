#ifndef RAYGUN_EVENT_H
#define RAYGUN_EVENT_H

#include "raygun_coercion.h"

extern VALUE rb_mRaygunApm;

// Ruby specific class references that map to the protocol event structs. All except the extended SQL and HTTP events are used only in testing
// for builing up event streams from the callback sink and assert expected sequences and attributes of the captured observed event streams.

extern VALUE rb_cRaygunEvent,
  rb_cRaygunEventBegin,
  rb_cRaygunEventEnd,
  rb_cRaygunEventMethodinfo,
  rb_cRaygunEventExceptionThrown,
  rb_cRaygunEventThreadStarted,
  rb_cRaygunEventThreadEnded,
  rb_cRaygunEventProcessStarted,
  rb_cRaygunEventProcessEnded,
  rb_cRaygunEventProcessFrequency,
  rb_cRaygunEventProcessType,
  rb_cRaygunEventBatch,
  rb_cRaygunEventSql,
  rb_cRaygunEventHttpIn,
  rb_cRaygunEventHttpOut,
  rb_cRaygunEventBeginTransaction,
  rb_cRaygunEventEndTransaction;

// Garbage collection callbacks
void rb_rg_event_free(void *ptr);
size_t rb_rg_event_size(const void *ptr);

// Coerces a Ruby object to the protocol event it represents
extern const rb_data_type_t rb_rg_event_type;
#define rb_rg_get_event(obj) \
  rg_event_t *event; \
  TypedData_Get_Struct(obj, rg_event_t, &rb_rg_event_type, event); \
  if (!event) rb_raise(rb_eRaygunFatal, "Could not initialize event"); \

// API specific to extended events called from Ruby code to encode and inject them into the dispatch ring buffer
VALUE rb_rg_event_encoded(VALUE obj);

void _init_raygun_event();

#endif
