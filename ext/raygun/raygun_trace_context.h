#ifndef RAYGUN_TRACE_CONTEXT_H
#define RAYGUN_TRACE_CONTEXT_H

#include "raygun_coercion.h"

struct rb_rg_tracer_t;

// A trace context represents a unit of work being instrumented and is setup at the start of eg. a request and torn down at the end
// To keep traces clean from auxiliary work such as DB connection pool cleanups etc. a Ruby Tracepoint is enabled only for the duration
// of a given trace boundary.

typedef struct _rb_rg_trace_context_t {
    // Reference to the main tracer struct - a parent relationship - tracer has many trace contexts
    struct rb_rg_tracer_t *tracer;
    // Pointer to a Ruby object representing the Tracepoint which invokes the tracer hook
    VALUE tracepoint;
    // The Ruby Thread that represents this unit of work - generally for example a Puma worker thread or similar
    VALUE thread;
    // For thread started event callbacks
    VALUE parent_thread;
    // The Thread Group this context belongs to - important for threads ancestry tracking
    VALUE thgroup;
    // The Shadow Thread for this trace context
    rg_thread_t *rg_thread;
} rb_rg_trace_context_t;

// Allocation helper
rb_rg_trace_context_t *rb_rg_trace_context_alloc(struct rb_rg_tracer_t *tracer, VALUE thread);

// Garbage collection callbacks
void rb_rg_trace_context_free(rb_rg_trace_context_t *trace_context);
size_t rb_rg_trace_context_size(rb_rg_trace_context_t *trace_context);
void rb_rg_trace_context_mark(rb_rg_trace_context_t *trace_context);

#endif
