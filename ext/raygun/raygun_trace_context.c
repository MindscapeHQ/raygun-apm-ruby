#include "raygun_tracer.h"

// Allocates a Trace Context. A Trace Context is a data structure that tracks the baseline state for a unit of work (request or background job)
// It can also be viewed as "Everything that happens within a block of BEGIN and END transaction commands".
// It tracks:
//
// * The current Ruby thread executing (a Puma worker thread from the server thread pool for example)
// * The shadow stack and pointers to the VM top (at point of trace entry) and shadow stack top (all inits to 0). A distinct VM and shadow top is tracked because
//   the profiler follows only until a maximum frame depth, then backs out
// * A reference to the profiler's shadow thread of the Ruby thread for this context (so we don't need to continuously look it up at runtime for the trace duration)
// * The ruby Tracepoint, which is explicitly enabled at the start of a BEGIN transaction command and disabled on END transaction to remove spurious code paths
//   hitting the tracepoint callback function which we're not interested anyways.
// * A reference to the parent Ruby Thread which spawned the executing Ruby Thread for this trace. Typically the main thread, which itself spawned the pool of worker
//   threads that serve requests.
//
// This allocator helper ensures a blank slate pristine trace context at the start of every trace.
//
rb_rg_trace_context_t *rb_rg_trace_context_alloc(struct rb_rg_tracer_t *tracer, VALUE thread)
{
    rb_rg_trace_context_t *trace_context;
    rg_thread_t *th = rb_rg_thread(tracer, thread);
    trace_context = ZALLOC(rb_rg_trace_context_t);
    if (trace_context == NULL) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate trace context\n");
    }
#endif
        rb_raise(rb_eRaygunFatal, "Could not allocate trace context");
    }
    trace_context->tracer = tracer;
    // executing Ruby Thread
    trace_context->thread = thread;
    // Thread Group (an Ruby feature for tracking ancestry of spawned threads) - assigned in raygun_tracer.c as some heavier lifting is required and we keep
    // this allocator helper simple
    trace_context->thgroup = Qnil;
    // Reset the shadow thread's observed state on starting a new trace
    th->shadow_top = RG_THREAD_FRAMELESS;
    // Reset the VM's observed state (current as of start of the trace) on starting a new trace
    th->vm_top = RG_THREAD_FRAMELESS;
    // An optimization to limit how deep we trace into the stack of third party libraries
    th->level_deep_into_third_party_lib = 0;
    // Technically not required as the ZALLOC would do the same, but lets be explicit about initialising to 0
    MEMZERO(th->shadow_stack, rg_function_id_t, RG_SHADOW_STACK_LIMIT);
    // Cache the Ruby Thread <=> shadow thread mapping so it's only looked up once for the duration of the trace
    trace_context->rg_thread = th;
    // Tracepoint reference - initialized in raygun_trace.c to keep this helper simple
    trace_context->tracepoint = Qnil;
    // Parent thread reference - assigned in raygun_tracer.c
    trace_context->parent_thread = Qnil;
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] Allocated trace context: %p\n", (void *)trace_context);
#endif
    RB_GC_GUARD(thread);
    return trace_context;
}

// Helper invoked by the GC once determined that there's no more references to this Trace Context
void rb_rg_trace_context_free(rb_rg_trace_context_t *trace_context)
{
    // Do nothing if already freed
    if (!trace_context) return;

    // Also disable the Tracepoint if still active AND enabled
    if (RTEST(trace_context->tracepoint)) {
       if (rb_tracepoint_enabled_p(trace_context->tracepoint)) {
          rb_tracepoint_disable(trace_context->tracepoint);
          trace_context->tracepoint = Qnil;
       }
    }
#ifdef RB_RG_DEBUG
    if (UNLIKELY(trace_context->tracer->loglevel >= RB_RG_TRACER_LOG_INFO && trace_context->tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] Freeing trace context: %p\n", (void *)trace_context);
#endif
    // Finaly free the Trace Context struct and explicitly nullify
    xfree(trace_context);
    trace_context = NULL;
}

// Size calculation for the Trace Context struct - simple in this case as the struct is mostly pointers and the shadow stack is otherwise factored into
// struct size
size_t rb_rg_trace_context_size(rb_rg_trace_context_t *trace_context)
{
    return sizeof(rb_rg_trace_context_t);
}

// Mark / tracing callback from the GC - we mark all the VALUEs (references to Ruby objects)
void rb_rg_trace_context_mark(rb_rg_trace_context_t *trace_context)
{
    rb_gc_mark(trace_context->tracepoint);
    rb_gc_mark_maybe(trace_context->thread);
    rb_gc_mark(trace_context->thgroup);
    rb_gc_mark_maybe(trace_context->parent_thread);
}