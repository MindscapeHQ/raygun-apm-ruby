#ifndef RAYGUN_TRACER_H
#define RAYGUN_TRACER_H

#include "raygun_coercion.h"
#include "raygun_event.h"
#include "raygun_trace_context.h"

#include "raygun_ringbuf.h"

#include "rax.h"

#define UNUSED(x) (__attribute__((x))

extern VALUE rb_mRaygunApm;
extern VALUE rb_cRaygunTracer;

// Log levels the tracer supports - defaults to NONE

enum rb_rg_tracer_loglevel_t
{
  RB_RG_TRACER_LOG_NONE = 0x1,
  RB_RG_TRACER_LOG_INFO = 0x2,
  RB_RG_TRACER_LOG_WARNING = 0x3,
  RB_RG_TRACER_LOG_ERROR = 0x4,
  RB_RG_TRACER_LOG_VERBOSE = 0x5,
  RB_RG_TRACER_LOG_DEBUG = 0x6,
  RB_RG_TRACER_LOG_EVERYTHING = 0x7,
  RB_RG_TRACER_LOG_BLACKLIST = 0x8
};

// For handling some quircks for Rails development mode and constant code reloading

enum rb_rg_tracer_environment_t
{
  RB_RG_TRACER_ENV_DEVELOPMENT = 0x1,
  RB_RG_TRACER_ENV_PRODUCTION = 0x2
};

#define RB_RG_TRACER_BUILTIN_METHODS_TRANSLATED 5

struct rb_rg_tracer_t;

#ifdef RG_ENCODE_ASYNC
typedef struct _rb_rg_async_event_t {
  rb_thread_t *current_thread;
  rb_rg_trace_context_t *trace_context;
  rb_trace_arg_t *tparg;
} rb_rg_async_event_t;

typedef struct _rb_rg_async_encoder_t {
  bool running;
  bipbuf_t *buffer;
} rb_rg_async_encoder_t;
#endif

// Container that represents the profiler's chosen sink state

typedef struct _rb_rg_sink_data_t {
    struct rb_rg_tracer_t *tracer;
    rg_ringbuf_t ringbuf;
    bool running;
    // XXX this should probably be an union
    // The UDP socket used by the UDP sink
    VALUE sock;
    // The closure used by the callback sink
    VALUE callback;
    // Additional members specific to the UDP sink
    VALUE host;
    VALUE port;
    VALUE payload;
    // Some statistics we track for the diagnostics feature
    size_t encoded_batched;
    size_t encoded_raw;
    size_t flushed;
    size_t resets;
    size_t batches;
    size_t max_buf_used;
    size_t bytes_sent;
    size_t failed_sends;
    size_t jittered_sends;
    // Max Kernel buffer we can rely on - set by calling option SO_RCVBUF on the UDP socket
    int receive_buffer_size;
    rg_event_batch_t batch;
} rb_rg_sink_data_t;

// The primary Tracer struct

typedef struct rb_rg_tracer_t {
  // Encoder specific context
  rg_context_t *context;
  // Counter of the number of threads we've observed - used for the thread IDs emitted via wire protocol
  rg_tid_t threads;
  // Counter for the number of whitelisted methods observed - used for the function IDs emitted via wire protocol
  rg_function_id_t methods;
  // Telemetry specific - the amount of observed methods deemed to be blacklisted
  rg_function_id_t blacklisted;
  // Rails specific - dev environment alternative handling of function index calculation to support code relaoding
  rg_byte_t environment;
  // Log level used - defaults to NONE (silent)
  rg_byte_t loglevel;
  // Blacklist debugging specific
  rg_byte_t debug_blacklist;
  // Container for the generic blacklist - matches opaque patterns
  rax *blacklist;
  // Container for the fully qualified blacklist - matches eg. Foo::Bar#baz
  rax *blacklist_fq;
  // Container for the paths blacklist - matches eg. Foo::Bar
  rax *blacklist_paths;
  // Container for the methods blacklist - matches eg. #baz
  rax *blacklist_methods;
  // Container for the paths considered library code
  rax *libraries;
  // Symbol table for methodinfo - tracks entries for both whitelisted and blacklisted methods
  st_table *methodinfo;
  // Symbol table for observed threads
  st_table *threadsinfo;
  // Symbol table for trace contexts - a trace context represents a unit of work being instrumented and is setup at the start of eg. a request and torn down at the end
  st_table *tracecontexts;
  // Mutex for when incrementing the function IDs observed
  rb_nativethread_lock_t method_lock;
  // Mutex for when incrementing the thread IDs observed
  rb_nativethread_lock_t thread_lock;
  // Static container for the technology type - emitted with the process type command
  VALUE technology_type;
  // Static container for the technology type - emitted with the process type command
  VALUE process_type;
  VALUE api_key;
  // Ruby thread that's responsible for dispatching encoded wire protocol data to the Agent. Currently only used by the UDP sink
  VALUE sink_thread;
  // Ruby thread that's responsible for periodically flushing the dispatch ring buffer under low volume conditions and also for syncing the methodinfo table with the agent regularly
  VALUE timer_thread;
#ifdef RG_ENCODE_ASYNC
  VALUE encoder_thread;
  rb_rg_async_encoder_t *async_encoder;
#endif
#ifdef RB_RG_EMIT_ARGUMENTS
  VALUE returnvalue_str;
  VALUE catch_all_arg;
  VALUE catch_all_arg_val;
#endif
  // Static container for encoding options used for the encoded string types supported by the wire protocol
  VALUE ecopts;
  // Sink / dispatch specific as documented in the struct above
  rb_rg_sink_data_t sink_data;
  // When set, shuts down the profiler for cases where a minimum Agent version is required, but an old one observed (would break the Agent with incompatibilities otherwise)
  rg_byte_t noop;
  // Lookup table for coercing Object, IO, Random, Signal and Thread::Mutex internal types from their Raygun::Apm::Hooks::* patches
  // to sidestep having to depend on the VERY expensive RUBY_EVENT_C_* events which kills performance of any tracing profiler
  st_table *builtin_translator;
  // Lookup table for method paths to be classified as synchronization method sources
  st_table *synchronization_methods;
} rb_rg_tracer_t;

// Garbage collector specific callbacks
void rb_rg_tracer_mark(void *ptr);
void rb_rg_tracer_free(void *ptr);
size_t rb_rg_tracer_size(const void *ptr);

static void rb_rg_process_type(const rb_rg_tracer_t *tracer);
static void rb_rg_process_frequency(const rb_rg_tracer_t *tracer, rg_frequency_t frequency);

// Optimized API to infer the Thread Group from a thread - skips a Ruby method call
VALUE rb_rg_thread_group(rb_thread_t *th);

// Lookup and coercion helper for Ruby Thread -> Shadow Thread
rg_thread_t *rb_rg_thread(rb_rg_tracer_t *tracer, VALUE thread);

static void rb_rg_tracing_hook_i0(rb_thread_t *current_thread, rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rb_trace_arg_t *tparg);

// Coerces a Ruby heap object to a tracer struct (Ruby object backed by the tracer struct)
extern const rb_data_type_t rb_rg_tracer_type;
#define rb_rg_get_tracer(obj) \
  rb_rg_tracer_t *tracer = NULL; \
  TypedData_Get_Struct(obj, rb_rg_tracer_t, &rb_rg_tracer_type, tracer); \
  if (UNLIKELY(!tracer)) rb_raise(rb_eRaygunFatal, "Could not initialize tracer"); \

// Lookup helper for the current Trace Context from the running Ruby Thread
#define rb_rg_get_current_thread_trace_context() \
  rb_thread_t *current_thread = GET_THREAD(); \
  rb_rg_trace_context_t *trace_context = NULL; \
  VALUE thread = current_thread->self; \
  VALUE thgroup = rb_rg_thread_group(current_thread); \
  st_lookup(tracer->tracecontexts, (st_data_t)thgroup, (st_data_t *)&trace_context); \
  RB_GC_GUARD(thread); \
  RB_GC_GUARD(thgroup); \

void _init_raygun_tracer();

#endif
