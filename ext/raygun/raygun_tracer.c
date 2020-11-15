#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#include "extconf.h"
#include "raygun_tracer.h"

// The Raygun::Tracer class setup in the Init_ function
VALUE rb_cRaygunTracer;

// static symbols (IDs) declared ahead of time and also initialized in Init_ to reduce intern (String -> ID) overhead at runtime
static ID rb_rg_id_send,
    rb_rg_id_name_equals,
    rb_rg_id_connect,
#ifdef RB_RG_EMIT_ARGUMENTS
    rb_rg_id_parameters,
    rb_rg_id_local_variable_get,
    rb_rg_id_catch_all,
#endif
    rb_rg_id_invalid,
    rb_rg_id_replace,
    rb_rg_id_config,
    rb_rg_id_loglevel,
    rb_rg_id_th_group,
    rb_rg_id_join,
    rb_rg_id_socket,
    rb_rg_id_host,
    rb_rg_id_port,
    rb_rg_id_receive_buffer_size,
    rb_rg_id_exception_correlation_ivar,
    rb_rg_id_message,
    rb_rg_id_write;

static VALUE rb_rg_cThGroup;

// The main typed data struct that helps to inform the VM (mostly the GC) on how to handle a wrapped structure
// References https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-
const rb_data_type_t rb_rg_tracer_type = {
    .wrap_struct_name = "rb_rg_tracer",
    .function = {
        .dmark = rb_rg_tracer_mark,
        .dfree = rb_rg_tracer_free,
        .dsize = rb_rg_tracer_size,
    },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/*
* This is to make -fstack-protector-all work in case of linking errors with libssp
* (which should define default implementations of these stubs).
*/
#ifndef HAVE___STACK_CHK_GUARD
unsigned long __stack_chk_guard;
void __stack_chk_guard_setup(void)
{
     __stack_chk_guard = 0xBAAAAAAD;
}
#endif
#ifndef HAVE___STACK_CHK_FAIL
void __stack_chk_fail(void)
{
 printf("Stack poisoning detected\n");
}
#endif

// Log errors silenced in timer and dispatch threads by rb_protect
static void rb_rg_log_silenced_error()
{
  VALUE msg = rb_check_funcall(rb_errinfo(), rb_rg_id_message, 0, 0);
  if (msg == Qundef || NIL_P(msg)) return;
  printf("[Raygun APM] error: %s\n", RSTRING_PTR(msg));
}

// A small wrapper called on shutdown that infers if the currently running thread is scheduled to be killed or not
int
rb_rg_current_thread_to_be_killed()
{
  rb_thread_t *th = GET_THREAD();

  if (th->to_kill || th->status == THREAD_KILLED) {
	  return true;
  }
  return false;
}

// A helper for pausing the current thread for a specific period of time but with awareness of the thread state (to kill or killed), which skips any sleep
// behaviour and lets the VM check for interrupts right away. Called from the timer thread which runs periodically and also from the UDP thread when the
// dispatch ringbuffer is empty or when we infer a very fast growth rate of the ringbuffer, but have > 50% room left, which allows for introducing jitter in
// the UDP dispatch thread in order to try to defer blowing through Kernel buffers too fast. It works OK, but isn't perfect, but better than nothing.
//
static void rb_rg_thread_wait_for(struct timeval tv){
  if (!rb_rg_current_thread_to_be_killed()) {
    rb_thread_schedule();
    rb_thread_wait_for(tv);
  }
  rb_thread_check_ints();
}

extern rax *raxNew(void);

// A callback function invoked by st_foreach in rb_rg_tracer_mark that marks the trace contexts table. VALUE object pointers in Ruby are Ruby heap allocated
// and as such the key (a Ruby thread) and also some members of the trace context needs to be marked by the GC.
//
static int rb_rg_trace_context_mark_i(st_data_t key, st_data_t val, st_data_t data)
{
  rb_rg_trace_context_t *trace_context = (rb_rg_trace_context_t *)val;
  rb_gc_mark((VALUE)key);
  rb_rg_trace_context_mark(trace_context);
  return ST_CONTINUE;
}

// A callback function invoked by st_foreach in rb_rg_tracer_mark that marks the threads info table. VALUE object pointers in Ruby are Ruby heap allocated
// and as such the key (a Ruby thread) needs to be marked, but conditionally with rb_gc_mark_maybe as the thread might be already killed and reclaimed under
// some circumstances. The value is a scalar integer value (TID in Raygun wire protocol and thus not a Ruby object)
//
static int rb_rg_threadsinfo_mark_i(st_data_t key, st_data_t val, st_data_t data)
{
  rb_gc_mark_maybe((VALUE)key);
  return ST_CONTINUE;
}

// The main GC hook that walks the struct that represents an instance of Raygun::Apm::Tracer during the tracing (mark) phase that verifies if objects are alive
// or not. Mostly concerned with the trace contexts table, the threads table, callback sink metadata and the timer and sink threads
//
void rb_rg_tracer_mark(void *ptr)
{
  const rb_rg_tracer_t *tracer = (rb_rg_tracer_t *)ptr;
  st_foreach(tracer->tracecontexts, rb_rg_trace_context_mark_i, 0);
  st_foreach(tracer->threadsinfo, rb_rg_threadsinfo_mark_i, 0);
  rb_gc_mark(tracer->sink_data.callback);
  rb_gc_mark(tracer->sink_data.sock);
  rb_gc_mark(tracer->sink_data.host);
  rb_gc_mark(tracer->sink_data.port);
  // Noop for UDP and TCP sinks, required for the callback sink
  rb_gc_mark(tracer->sink_data.payload);
  rb_gc_mark(tracer->timer_thread);
  rb_gc_mark(tracer->sink_thread);
}

// A callback function invoked by walking the trace contexts table in function rb_rg_tracer_free. Frees the trace context struct and data it references and
// returns ST_DELETE, which instructs the Ruby symbol table implementation to remove this trace context from the table too (the entry effectively)
//
static int rb_rg_trace_context_free_i(st_data_t key, st_data_t val, st_data_t data)
{
  rb_rg_trace_context_free((rb_rg_trace_context_t*) val);
  return ST_DELETE;
}

// A callback function invoked by walking the trace contexts table in function rb_rg_tracer_free. Returns ST_DELETE, which instructs the Ruby symbol table
// implementation to remove this trace context from the table too (the entry effectively). There's nothing to free because the key (Ruby Thread) is a VALUE
// and the mark callback is a hint for the GC to collect it, or not. We're just concerned with removing dead threads from the symbol table otherwise it would
// just grow unbounded.
//
static int rb_rg_threadsinfo_free_i(st_data_t key, st_data_t val, st_data_t data)
{
  // Nothing to free, effectively just removes the entry
  return ST_DELETE;
}

// A callback function invoked by walking the methodinfo table in function rb_rg_tracer_free. Frees the rg_method struct and data it references and
// returns ST_DELETE, which instructs the Ruby symbol table implementation to remove this whitelisted shadow method from the table too (the entry effectively)
//
int rb_rg_methodinfo_free_i(st_data_t key, st_data_t val, st_data_t data)
{
  rg_method_t *rg_method = (rg_method_t *)val;
  // Blacklisted
  if ((int)val == RG_BLACKLIST_BLACKLISTED) return ST_DELETE;
  free(rg_method->encoded);
  xfree(rg_method->name);
  xfree(rg_method);
  rg_method = NULL;
  return ST_DELETE;
}

// The main GC callback from the typed data (https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-) struct.
// Typically it's the responsibility of this method to walk all struct members with data to free so that at the end of this function, if we free the profiler
// struct, there's nothing dangling about on the heap.
//
void rb_rg_tracer_free(void *ptr)
{
  // If already free, nothing to do here, early return
  if (!ptr) return;
  // Coerce the void pointer to a tracer struct
  rb_rg_tracer_t *tracer = (rb_rg_tracer_t *)ptr;
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] Tracer %p ctx: %p freed %p\n", (void *)tracer, (void *)tracer->context);
#endif
  // Destroy the thread safety locks previous initialized when the trace object was created (used for locking the methodinfo table on insert and also the threads
  // one on insert and delete)
  rb_nativethread_lock_destroy(&tracer->method_lock);
  rb_nativethread_lock_destroy(&tracer->thread_lock);
  // Free for UDP and other transport oriented sinks - no bipbuf allocated for callback sink
  if ((tracer->sink_data.type == RB_RG_TRACER_SINK_UDP || tracer->sink_data.type == RB_RG_TRACER_SINK_TCP) && RTEST(tracer->sink_data.sock))
    bipbuf_free(tracer->sink_data.ringbuf.bipbuf);

  // Free the source of truth for the radix trees
  raxFree(tracer->blacklist);
  raxFree(tracer->blacklist_fq);
  raxFree(tracer->blacklist_paths);
  raxFree(tracer->blacklist_methods);
  // Free the source of truth for external libraries
  raxFree(tracer->libraries);
  // Clean up trace contexts
  st_foreach(tracer->tracecontexts, rb_rg_trace_context_free_i, 0);
  // ... then free the symbol table too
  st_free_table(tracer->tracecontexts);
  // Explicitly nullify
  tracer->tracecontexts = NULL;

  // Thread ID mappings
  st_foreach(tracer->threadsinfo, rb_rg_threadsinfo_free_i, 0);
  // ... then free the symbol table too
  st_free_table(tracer->threadsinfo);
  // Explicitly nullify
  tracer->threadsinfo = NULL;

  // Global methodinfo table
  st_foreach(tracer->methodinfo, rb_rg_methodinfo_free_i, 0);
  // ... then free the symbol table too
  st_free_table(tracer->methodinfo);
  // Explicitly nullify
  tracer->methodinfo = NULL;

  // Free the special builtin method handler table - nothing allocated, st_free_table drains the static entries
  if (tracer->builtin_translator->num_entries == RB_RG_TRACER_BUILTIN_METHODS_TRANSLATED) {
      st_free_table(tracer->builtin_translator);
      // Explicitly nullify
      tracer->builtin_translator = NULL;
  }

  // Synchronization method table
  st_free_table(tracer->synchronization_methods);
  // Explicitly nullify
  tracer->synchronization_methods = NULL;

#ifdef RB_RG_EMIT_ARGUMENTS
  rb_gc_unregister_address(&tracer->returnvalue_str);
  rb_gc_unregister_address(&tracer->catch_all_arg);
  rb_gc_unregister_address(&tracer->catch_all_arg_val);
#endif
  // Remove the special GC registration to ALWAYS consider the encoding options hash as in use
  rb_gc_unregister_address(&tracer->ecopts);
  // Remove the special GC registration to ALWAYS consider the technology type string as in use
  rb_gc_unregister_address(&tracer->technology_type);
  // Remove the special GC registration to ALWAYS consider the process type string as in use
  rb_gc_unregister_address(&tracer->process_type);
  // Unregister for UDP or other transport oriented sinks only
  if ((tracer->sink_data.type == RB_RG_TRACER_SINK_UDP || tracer->sink_data.type == RB_RG_TRACER_SINK_TCP) && RTEST(tracer->sink_data.sock))
    rb_gc_unregister_address(&tracer->sink_data.payload);
  // Finally free the tracer
  xfree(tracer);
  // Explicitly nullify
  tracer = NULL;
}

// Required on any changes to the blacklist - rebuild the table from scratch for consistency
static void rb_rg_flush_caches(rb_rg_tracer_t *tracer)
{
  st_foreach(tracer->methodinfo, rb_rg_methodinfo_free_i, 0);
}

// A helper function to calculate the size in bytes of the trace context table values (accumulator)
static int rb_rg_add_trace_context_size_i(st_data_t key, st_data_t val, st_data_t data)
{
  size_t *size = (size_t *)data;
  *size += rb_rg_trace_context_size((rb_rg_trace_context_t*)val);
  return ST_CONTINUE;
}

// A helper function to calculate the size in bytes of the method info table values (accumulator)
static int rb_rg_add_methodinfo_size_i(st_data_t key, st_data_t val, st_data_t data)
{
  size_t *size = (size_t *)data;
  rg_method_t *rg_method = (rg_method_t *)val;
  // Blacklisted, count the int and early return
  if ((int)val == RG_BLACKLIST_BLACKLISTED) {
    *size += sizeof(int);
    return ST_CONTINUE;
  }
  *size += sizeof(rg_method_t);
  *size += rg_method->encoded_size;
  *size += rg_method->length;
  return ST_CONTINUE;
}

// Used by ObjectSpace to estimate the size of a Ruby object. This needs to account for all the retained memory of the object and requires walking any
// collection specific struct members and anything else malloc heap allocated.
//
size_t rb_rg_tracer_size(const void *ptr)
{
  size_t size;
  const rb_rg_tracer_t *tracer = (rb_rg_tracer_t *)ptr;
  size =  sizeof(rb_rg_tracer_t) +
          sizeof(rg_context_t) +
          // XXX revisit - this is the amount of node elements in the tree, NOT memory size but also very hard to estimate given the many algos the radix
          // tree uses internally
          raxSize(tracer->blacklist) +
          raxSize(tracer->blacklist_fq) +
          raxSize(tracer->blacklist_paths) +
          raxSize(tracer->blacklist_methods) +
          raxSize(tracer->libraries) +
          // calculate the memory size of the individual symbol table too (just the key value pairs as represented, NOT what they point to)
          st_memsize(tracer->tracecontexts) +
          st_memsize(tracer->methodinfo) +
          st_memsize(tracer->threadsinfo);
  // Add the ringbuffer allocated size, for transport oriented sinks
  if (RTEST(tracer->sink_data.sock)) size += bipbuf_size(tracer->sink_data.ringbuf.bipbuf);
  // Now add the values of the trace contexts table as well
  st_foreach(tracer->tracecontexts, rb_rg_add_trace_context_size_i, (st_data_t)&size);
  // Now add the values of the methodinfo table as well
  st_foreach(tracer->methodinfo, rb_rg_add_methodinfo_size_i, (st_data_t)&size);
  return size;
}

#ifdef RB_RG_DEBUG
static const char* rb_rg_event_type_to_str(const rg_event_t *event)
{
  switch((rg_event_type_t)event->type)
  {
    case RG_EVENT_BEGIN:
      return "BEGIN";
    case RG_EVENT_END:
      return "END";
    case RG_EVENT_METHODINFO_2:
      return "METHODINFO";
    case RG_EVENT_EXCEPTION_THROWN_2:
      return "EXCEPTION_THROWN";
    case RG_EVENT_THREAD_STARTED_2:
      return "THREAD_STARTED";
    case RG_EVENT_THREAD_ENDED:
      return "THREAD_ENDED";
    case RG_EVENT_PROCESS_ENDED:
      return "PROCESS_ENDED";
    case RG_EVENT_PROCESS_FREQUENCY:
      return "PROCESS_FREQUENCY";
    case RG_EVENT_BATCH:
      return "BATCH";
    case RG_EVENT_SQL_INFORMATION:
      return "SQL_INFORMATION";
    case RG_EVENT_HTTP_INCOMING_INFORMATION:
      return "HTTP_INCOMING_INFORMATION";
    case RG_EVENT_HTTP_OUTGOING_INFORMATION:
      return "HTTP_OUTGOING_INFORMATION";
    case RG_EVENT_PROCESS_TYPE:
      return "PROCESS_TYPE";
   case RG_EVENT_BEGIN_TRANSACTION:
      return "BEGIN_TRANSACTION";
   case RG_EVENT_END_TRANSACTION:
      return "END_TRANSACTION";
    default:
      return "UNKNOWN";
  }
}
#endif

// Used by the callback sink frequently used in tests for the protocol to Ruby event class mapping
static VALUE rb_rg_event_type_to_class(const rg_event_t *event)
{
  switch((rg_event_type_t)event->type)
  {
    case RG_EVENT_BEGIN:
      return rb_cRaygunEventBegin;
    case RG_EVENT_END:
      return rb_cRaygunEventEnd;
    case RG_EVENT_METHODINFO_2:
      return rb_cRaygunEventMethodinfo;
    case RG_EVENT_EXCEPTION_THROWN_2:
      return rb_cRaygunEventExceptionThrown;
    case RG_EVENT_THREAD_STARTED_2:
      return rb_cRaygunEventThreadStarted;
    case RG_EVENT_THREAD_ENDED:
      return rb_cRaygunEventThreadEnded;
    case RG_EVENT_PROCESS_ENDED:
      return rb_cRaygunEventProcessEnded;
    case RG_EVENT_PROCESS_FREQUENCY:
      return rb_cRaygunEventProcessFrequency;
    case RG_EVENT_BATCH:
      return rb_cRaygunEventBatch;
    case RG_EVENT_SQL_INFORMATION:
      return rb_cRaygunEventSql;
    case RG_EVENT_HTTP_INCOMING_INFORMATION:
      return rb_cRaygunEventHttpIn;
    case RG_EVENT_HTTP_OUTGOING_INFORMATION:
      return rb_cRaygunEventHttpOut;
    case RG_EVENT_PROCESS_TYPE:
      return rb_cRaygunEventProcessType;
    case RG_EVENT_BEGIN_TRANSACTION:
      return rb_cRaygunEventBeginTransaction;
    case RG_EVENT_END_TRANSACTION:
      return rb_cRaygunEventEndTransaction;
    default:
      return Qnil;
  }
}

// Invokes the block / closure - we pass the callback Proc instance and it's argument (payload) through a sink data struct
// Extracted to a distinct function to be invoked by rb_protect (profiler resiliency)
static VALUE rb_rg_callback_sink_call(VALUE ptr)
{
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  rb_proc_call_with_block(data->callback, 1, &data->payload, Qnil);
  return Qtrue;
}

// Sink that calls a registered ruby Proc and coerces raw Raygun wire protocol events to wrapped structs (objects) allocated on the Ruby heap
// Invokves the callback sink closure with rb_protect in order to handle any runtime errors cleanly without blowing up the tracer instance
static int rb_rg_callback_sink(rg_context_t *context, rb_rg_sink_data_t *sink_data, rg_event_t *event, const rg_length_t size)
{
  int status = 0;
  rg_event_t *event_copy;
  VALUE wrapped_event;

  // In reality this can never happen, but check nonetheless
  if (!sink_data->callback)
    return -1;

  // Ditto
  if (!event)
    return -1;

  // Work with copies of the raw events and wrap them as a Ruby object
  event_copy = ZALLOC(rg_event_t);
  memcpy(event_copy, event, sizeof(rg_event_t));
  wrapped_event = TypedData_Wrap_Struct(rb_rg_event_type_to_class(event), &rb_rg_event_type, event_copy);
  sink_data->payload = wrapped_event;
  rb_protect(rb_rg_callback_sink_call, (VALUE)sink_data, &status);
  if (UNLIKELY(status)) {
    rb_rg_log_silenced_error();
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    return -1;
  }
  RB_GC_GUARD(wrapped_event);
  return 1;
}

// Peek into the transport specific ring buffer for the next expected message size we can consume
static inline rg_length_t rg_ringbuf_next_message_size(const rg_ringbuf_t *ringbuf)
{
  // XXX dereference instead of memcpy?
  rg_length_t size;
  unsigned char *buf = NULL;
  buf = bipbuf_peek(ringbuf->bipbuf, (unsigned int)sizeof(size));
  if(buf)
  {
    memcpy(&size, buf, sizeof(size));
  } else
  {
    return 0;
  }  
  return size;
}

// Resets the batch struct on sink data to a fresh state. We don't touch the sequence number, just reset it's length to the header
// size (which is a space reservation as we fill it in on handoff to the ring buffer for dispatch with rg_encode_batch_header)
// and resets the commands count for the current batch to 0
//
static inline void rb_rg_spawn_new_batch(rb_rg_sink_data_t *sink_data)
{
    sink_data->batch.length = RG_BATCH_HEADLEN;
    sink_data->batch.count = 0;
    sink_data->resets++;
    sink_data->batches++;
}

#ifdef RB_RG_DEBUG
static inline char* rb_rg_tracer_sink_name(rb_rg_sink_data_t *sink_data)
{
  switch(sink_data->type){
    case RB_RG_TRACER_SINK_TCP:
          return "TCP";
    case RB_RG_TRACER_SINK_UDP:
          return "UDP";
  }
}
#endif

// Sink that emits a UDP packet. This callback could be more generic, perhaps rb_rg_batched_sink as it ensures a stream
// of MTU sized batches and could also be directly usable by a TCP transport by just changing the naming and having a TCP
// dispatcher thread.
//
static int rb_rg_batched_sink(rg_context_t *context, void *userdata, const rg_event_t *event, const rg_length_t buflen)
{
  rb_rg_sink_data_t *sink_data = (rb_rg_sink_data_t *)userdata;
#ifdef RB_RG_DEBUG
  const struct rb_rg_tracer_t *tracer = sink_data->tracer;
#endif
  int retval = 1;
  size_t buf_used = bipbuf_used(sink_data->ringbuf.bipbuf);

  // Tracks the maximum size of the ring buffer used to facilitate the jitter buffer feature for UDP sinks and also used in telemetry when the
  // RAYGUN_DIAGNOSTICS env var is set
  if (buf_used > sink_data->max_buf_used) {
     sink_data->max_buf_used = buf_used;
  }

  // The most frequent path most encoded events pass through - the batch is still smaller than MTU size, append to batch
  if (LIKELY(event && (sink_data->batch.length + buflen) <= RG_BATCH_PACKET_SIZE))
  {
    // room for extra data in current batch, encode in batch
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] %s sink batch %u, smaller than batch packet size %u, room in current batch, encode %s into batch\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, RG_BATCH_PACKET_SIZE, rb_rg_event_type_to_str(event));
#endif
    // Append a command to the current batch
    rg_encode_into_batch(context->buf, buflen, &sink_data->batch);
    sink_data->encoded_batched++;
  } else if (!event || (RG_BATCH_HEADLEN+buflen <= RG_BATCH_PACKET_SIZE))
  {
    // The only time we expect a NULL event is from the timer thread on tick to force flush any partial batches at a 1s cadence so we don't have cruft accumulating
    // and delay traces from being finalized at the Agent layer.
    // Do not attempt to flush empty batches though
    //
    if (sink_data->batch.length == RG_BATCH_HEADLEN) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] Not flushing empty batch\n");
#endif
      return retval;
    }
    // flush batch (null event)
    // or room in a new batch so dispatch current + encode in a new batch
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      if (!event) {
        printf("[Raygun APM] %s sink batch %u, null event, finalize batch\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence);
      } else {
        printf("[Raygun APM] %s sink batch %u, smaller than batch packet size %u, room in a new batch so dispatch current, encode %s into batch\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, RG_BATCH_PACKET_SIZE, rb_rg_event_type_to_str(event));
      }
    }
#endif
    // Batch header is always encoded last as it needs the batch to be finalized before being able to generate a represetantive header
    rg_encode_batch_header(&sink_data->batch);
    sink_data->batches++;

    // Add the batch to the dispatch ring buffer for emission
    retval = bipbuf_offer(sink_data->ringbuf.bipbuf, (unsigned char*)sink_data->batch.buf, (int)(sink_data->batch.length));
#ifdef RB_RG_DEBUG
    if (UNLIKELY(retval == 0))
    {
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
        printf("[Raygun APM] %s sink batch %u overflow wanted:%i used:%i unused:%i!\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, buflen, bipbuf_used(sink_data->ringbuf.bipbuf), bipbuf_unused(sink_data->ringbuf.bipbuf));
        assert(retval == 0);
      }
    } else
    {
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] %s sink batch %u queued:%i used:%i unused:%i\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, buflen, bipbuf_used(sink_data->ringbuf.bipbuf), bipbuf_unused(sink_data->ringbuf.bipbuf));
    }
#endif
    // Reset the batch back to 0 batch count, retain sequence number
    rb_rg_spawn_new_batch(sink_data);
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] %s sink - reset batch\n", rb_rg_tracer_sink_name(sink_data));
#endif
    // encode in batch
    if (event) {
      // Append a command to the current batch
      rg_encode_into_batch(context->buf, buflen, &sink_data->batch);
      sink_data->encoded_batched++;
    }
  } else
  {
    // buflen exceeds RG_MAX_BATCH_PACKET_SIZE, send as-is - best effort delivery depending on transport, probably :boom: for UDP, likely delivered for TCP
    if (buflen > RG_MAX_BATCH_PACKET_SIZE) {
      // make no attempt to wrap it into a batch command
      retval = bipbuf_offer(sink_data->ringbuf.bipbuf, (unsigned char*)context->buf, (int)(buflen));
      // Reset the batch back to 0 batch count, retain sequence number
      rb_rg_spawn_new_batch(sink_data);
      sink_data->encoded_raw++;
    } else {
      // Flush current batch but also spawn a new batch for the payload that exceeds the default batch size
      // These are edge cases for SQL queries etc.
      rg_encode_batch_header(&sink_data->batch);
      sink_data->batches++;
      bipbuf_offer(sink_data->ringbuf.bipbuf, (unsigned char*)sink_data->batch.buf, (int)(sink_data->batch.length));

      // Spawn the new batch with the event sized > MTU but smaller than RG_BATCH_PACKET_SIZE (typically a SQL query event)
      rb_rg_spawn_new_batch(sink_data);
      // Append a command to the current batch
      rg_encode_into_batch(context->buf, buflen, &sink_data->batch);
      // Batch header is always encoded last as it needs the batch to be finalized before being able to generate a represetantive header
      rg_encode_batch_header(&sink_data->batch);
      retval = bipbuf_offer(sink_data->ringbuf.bipbuf, (unsigned char*)sink_data->batch.buf, (int)(sink_data->batch.length));

      // Spawn a fresh empty batch for subsequent commands that follow the large SQL query
      rb_rg_spawn_new_batch(sink_data);
    }
#ifdef RB_RG_DEBUG
    if (retval == 0)
    {
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] %s sink batch %u overflow wanted:%i used:%i unused:%i!\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, buflen, bipbuf_used(sink_data->ringbuf.bipbuf), bipbuf_unused(sink_data->ringbuf.bipbuf));
    } else
    {
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] %s sink batch %u queued:%i used:%i unused:%i\n", rb_rg_tracer_sink_name(sink_data), sink_data->batch.sequence, buflen, bipbuf_used(sink_data->ringbuf.bipbuf), bipbuf_unused(sink_data->ringbuf.bipbuf));
    }
#endif
  }
  // Give the transport specific sender thread a slice since we generally fill faster than consume
  rb_thread_schedule();
  return retval;
}

// Wrapped function call and this kind of isn't great that we need to invoke rb_funcall, but the Socket extension does not provide low level APIs and it would
// be crazy trying to implement a low level UDP dispatcher from scratch that supports all platforms flawlessly and end up in a better place than Ruby.
// The cost is neglible though as it's invoked async from a dispatcher thread though.
//
static VALUE rb_rg_udp_sink_send(VALUE ptr)
{
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  return rb_funcall(data->sock, rb_rg_id_write, 1, data->payload);
}

// Wrapped function call and this kind of isn't great that we need to invoke rb_funcall, but the Socket extension does not provide low level APIs and it would
// be crazy trying to implement a low level TCP dispatcher from scratch that supports all platforms flawlessly and end up in a better place than Ruby.
// The cost is neglible though as it's invoked async from a dispatcher thread though.
//
static VALUE rb_rg_tcp_sink_send(VALUE ptr)
{
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  return rb_funcall(data->sock, rb_rg_id_send, 4, data->payload, INT2NUM(0), data->host, data->port);
}

// Force flush a batched sink with a special case NULL event
static void rb_rg_flush_batched_sink(const rb_rg_tracer_t *tracer)
{
  // Formal contract: pass NULL event to flush BATCHED sinks
  tracer->context->sink(tracer->context, (void *)&tracer->sink_data, NULL, 0);
}

// Syncs an already encoded methodinfo event with the agent to reduce chicken and egg between profiler and Agent. This is a callback from st_foreach
// on the methodinfo table to guard against cases where the profiled proces is up, but the Agent dies where it now effectively has a 0 sized methodinfo
// table state built up agent side. This sync happens every 30 seconds and triggered by the timer thread.
//
static int rb_rg_async_emit_methodinfo_i(st_data_t key, st_data_t val, st_data_t data)
{
  // Fake methodinfo event spawned for syncing state exlusively
  rg_event_t event;
  event.type = RG_EVENT_METHODINFO_2;
  rb_rg_tracer_t *tracer = (rb_rg_tracer_t *)data;
  rg_method_t *rg_method = (rg_method_t *)val;
  // Blacklisted, nothing to emit
  if ((int)val == RG_BLACKLIST_BLACKLISTED) return ST_CONTINUE;
#ifdef RB_RG_DEBUG
  if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
    printf("[Raygun APM] Async emit methodinfo for function %u (%lu bytes) from timer thread\n", rg_method->function_id, rg_method->encoded_size);
#endif
  // Copy the already encoded methodinfo event back into the encoder scratch buffer for handoff to the transport dispatch thread
  memcpy(tracer->context->buf, rg_method->encoded, rg_method->encoded_size);
  tracer->context->sink(tracer->context, (void *)&tracer->sink_data, &event, rg_method->encoded_size);
  return ST_CONTINUE;
}

// Re-syncs the current global method table (whitelisted methods this process has seen) with the Agent, in case the Agent died and comes back up,
// effectively orphaned from any previously methodinfo table state.
static void rb_rg_async_emit_methodinfos(const rb_rg_tracer_t *tracer) {
  // No need to emit anything if the methodinfo table is empty
  if (UNLIKELY(tracer->methodinfo->num_entries == 0)) return;
  // No need to emit anything if we're not using a transport oriented sink
  if (UNLIKELY(!RTEST(tracer->sink_data.sock))) return;
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Syncing the global whitelisted method table with the Agent\n");
    }
#endif
  // Emit frequency and process type again alongside this in case the Agent just came back up and haven't received these events yet
  rb_rg_process_frequency(tracer, (rg_frequency_t)TIMESTAMP_UNITS_PER_SECOND);
  rb_rg_process_type(tracer);
  st_foreach(tracer->methodinfo, rb_rg_async_emit_methodinfo_i, (st_data_t)tracer);
}

// A timer thread spawned to handle period work, one of two units:
// * Flush any partial batches typically left over at the end of a unit of work to ensure a constant and correct flow of data to the Agent
// * Periodic sync of the methodinfo table with the Agent
// Exits when the tracer shuts down (data->running is set to false and the main loop quits)
// 
static VALUE rb_rg_timer_thread(void *ptr)
{
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  const rb_rg_tracer_t *tracer = data->tracer;
  // XXX to get from tracer config, static default of PROTON_BATCH_IDLE_COUNTER=500 to start with
  struct timeval tv;
  tv.tv_sec = RG_TIMER_THREAD_TICK_INTERVAL;
  tv.tv_usec = 0;
  int methodinfo_sync_ticks = 0;
  while(data->running) {
    rb_rg_thread_wait_for(tv);
    // Flush out any commands still in a partial batch periodically to ensure a constant flow of data to the Agent
    rb_rg_flush_batched_sink(tracer);
    if (UNLIKELY(methodinfo_sync_ticks == RG_TIMER_THREAD_METHODINFO_TICK)) {
      // Sync the methodinfo table periodically with the Agent
      rb_rg_async_emit_methodinfos(tracer);
      methodinfo_sync_ticks = 0;
    }
    methodinfo_sync_ticks++;
  }
  return Qtrue;
}

// The main sink thread that is responsible for driving UDP dispatch. This thread is the other end of the bipbuf (ring buffer)
// and is the only consumer of it. The dispatch main loop balances sending as fast as possible when the buffer has data to send
// but also periodically goes to sleep for up to 1s in order to not negatively impact CPU when the profiler isn't doing any work.
//
static VALUE rb_rg_udp_sink_thread(void *ptr)
{
  int status = 0;
  int bytes_to_send_on_wakeup = 0;
  rg_short_t size;
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = RG_SINK_THREAD_TICK_INTERVAL;
#ifdef RB_RG_DEBUG
  const struct rb_rg_tracer_t *tracer = data->tracer;
#endif

  // data->running is set to false on profiler shutdown which terminates this main loop and allows this thread to exit
  while(data->running || !bipbuf_is_empty(data->ringbuf.bipbuf))
  {
    bytes_to_send_on_wakeup = 0;

    // On each wakeup try to flush the queue, if there's anything to flush
    while(!bipbuf_is_empty(data->ringbuf.bipbuf))
    {
      // Peek into the bipbuf to determine the next expected message size to send
      size = rg_ringbuf_next_message_size(&data->ringbuf);
      bytes_to_send_on_wakeup += size;

      // On empty next buffered message, terminate this UDP thread
      if (UNLIKELY(!(size > 0))) {
        data->running = false;
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] UDP thread terminating - next buffered message is empty\n");
#endif
        break;
      }
      // As above, but moves the ring buffer cursor
      unsigned char *ptr = bipbuf_poll(data->ringbuf.bipbuf, (unsigned int)size);
      if (UNLIKELY(!ptr)) {
        data->running = false;
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] UDP thread terminating - NULL message on polling buffer\n");
#endif
        break;
      }

      // Introduce slight jitter and preempt the dispatch thread if we're still under half of the bipbuf capacity
      // and receive buffer defaults (Linux net.core.rmem_default=212992 etc.) has not been exceeded. This jitter
      // buffer feature slows the dispatch down somewhat under high bipbuf growth AS LONG AS there's buffer capacity
      // available.
      //
      if (bytes_to_send_on_wakeup >= data->receive_buffer_size) {
        if (bipbuf_used(data->ringbuf.bipbuf) <= (RG_RINGBUF_SIZE / 2)) {
          rb_rg_thread_wait_for(tv);
          data->jittered_sends++;
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_WARNING && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("PID [%u] JITTERED jitters: %lu bipbuf: %u bytes_to_send_on_wakeup: %u Agent receive_buffer_size: %u\n", data->tracer->context->pid, data->jittered_sends, bipbuf_used(data->ringbuf.bipbuf), bytes_to_send_on_wakeup, data->receive_buffer_size);
#endif
          bytes_to_send_on_wakeup = 0;
        } else {
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_WARNING && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("PID [%u] No jitter, buffer fill rate too high: %u of %u\n", data->tracer->context->pid, bipbuf_used(data->ringbuf.bipbuf), RG_RINGBUF_SIZE);
#endif
        }
      }

      // Reset and fill the pre-allocated Ruby String buffer. This object is always considered as "marked" (in use) by the GC, won't be recycled until the profiler
      // shuts down and this pattern saves on Ruby heap allocation overhead per UDP packet (batch or exceptional oversized) sent
      rb_str_set_len(data->payload, 0);
      rb_str_buf_cat(data->payload, (const char *)ptr, size);

      // Call the actual UDP send function with rb_protect, which prevents raising a runtime exception - we catch the status and reset Ruby error info to NULL to prevent
      // an exception raised for the caught exception (if any). We increment the failed_sends telemetry counter which can be inspected when the PROTON_DIAGNOSTICS env
      // var is set.
      //
      rb_protect(rb_rg_udp_sink_send, (VALUE)data, &status);
      if (UNLIKELY(status)) {
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] UDP thread failed to send\n");
#endif
        rb_rg_log_silenced_error();
        // Clearing error info to ignore the caught exception
        rb_set_errinfo(Qnil);
        data->failed_sends++;
      } else {
        data->bytes_sent += size;
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] UDP sent:%i used:%i unused:%i\n", size, bipbuf_used(data->ringbuf.bipbuf), bipbuf_unused(data->ringbuf.bipbuf));
#endif
      }
    }
    if (LIKELY(data->running))
    {
      // While not instructed to exit, take a small pause to not burn CPU unecessary
      rb_rg_thread_wait_for(tv);
    } else
    {
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] UDP queue empty, exiting\n");
#endif
    }
  }
  return Qtrue;
}

// The main sink thread that is responsible for driving TCP dispatch. This thread is the other end of the bipbuf (ring buffer)
// and is the only consumer of it. The dispatch main loop balances sending as fast as possible when the buffer has data to send
// but also periodically goes to sleep for up to 1s in order to not negatively impact CPU when the profiler isn't doing any work.
//
static VALUE rb_rg_tcp_sink_thread(void *ptr)
{
  int status = 0;
  int bytes_to_send_on_wakeup = 0;
  rg_short_t size;
  rb_rg_sink_data_t *data = (rb_rg_sink_data_t *)ptr;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = RG_SINK_THREAD_TICK_INTERVAL;
#ifdef RB_RG_DEBUG
  const struct rb_rg_tracer_t *tracer = data->tracer;
#endif

  // data->running is set to false on profiler shutdown which terminates this main loop and allows this thread to exit
  while(data->running || !bipbuf_is_empty(data->ringbuf.bipbuf))
  {
    bytes_to_send_on_wakeup = 0;

    // On each wakeup try to flush the queue, if there's anything to flush
    while(!bipbuf_is_empty(data->ringbuf.bipbuf))
    {
      // Peek into the bipbuf to determine the next expected message size to send
      size = rg_ringbuf_next_message_size(&data->ringbuf);
      bytes_to_send_on_wakeup += size;

      // On empty next buffered message, terminate this UDP thread
      if (UNLIKELY(!(size > 0))) {
        data->running = false;
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] TCP thread terminating - next buffered message is empty\n");
#endif
        break;
      }
      // As above, but moves the ring buffer cursor
      unsigned char *ptr = bipbuf_poll(data->ringbuf.bipbuf, (unsigned int)size);
      if (UNLIKELY(!ptr)) {
        data->running = false;
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] TCP thread terminating - NULL message on polling buffer\n");
#endif
        break;
      }

      // Reset and fill the pre-allocated Ruby String buffer. This object is always considered as "marked" (in use) by the GC, won't be recycled until the profiler
      // shuts down and this pattern saves on Ruby heap allocation overhead per UDP packet (batch or exceptional oversized) sent
      rb_str_set_len(data->payload, 0);
      rb_str_buf_cat(data->payload, (const char *)ptr, size);

      // Call the actual TCP send function with rb_protect, which prevents raising a runtime exception - we catch the status and reset Ruby error info to NULL to prevent
      // an exception raised for the caught exception (if any). We increment the failed_sends telemetry counter which can be inspected when the PROTON_DIAGNOSTICS env
      // var is set.
      //
      rb_protect(rb_rg_tcp_sink_send, (VALUE)data, &status);
      if (UNLIKELY(status)) {
#ifdef RB_RG_DEBUG
        if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
          printf("[Raygun APM] TCP thread failed to send\n");
#endif
        // Clearing error info to ignore the caught exception
        rb_set_errinfo(Qnil);
        data->failed_sends++;
      } else {
        data->bytes_sent += size;
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] TCP sent:%i used:%i unused:%i\n", size, bipbuf_used(data->ringbuf.bipbuf), bipbuf_unused(data->ringbuf.bipbuf));
#endif
      }
    }
    if (LIKELY(data->running))
    {
      // While not instructed to exit, take a small pause to not burn CPU unecessary
      rb_rg_thread_wait_for(tv);
    } else
    {
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] TCP queue empty, exiting\n");
#endif
    }
  }
  return Qtrue;
}

// Tracing APIs

#ifdef RB_RG_EMIT_ARGUMENTS
static void rb_rg_tracepoint_parameters(const rb_rg_tracer_t *tracer, VALUE binding, VALUE params, rg_variable_info_t args[])
{
  // value of the params arg:
  // rb_method_parameters
  // *    def foo(bar, baz, *args, &blk); end
  // *    method(:foo).parameters #=> [[:req, :bar], [:req, :baz], [:rest, :args], [:block, :blk]]
  // rb_proc_parameters
  // *    prc = lambda{|x, y=42, *other|}
  // *    prc.parameters  #=> [[:req, :x], [:opt, :y], [:rest, :other]]
  int i;
  VALUE argtype, param, value;
  rg_variable_info_t arg;
  for (i = 0; i < RARRAY_LEN(params); i++)
  {
    argtype = RARRAY_AREF(params, i);
    param = RARRAY_AREF(argtype, 1);
    /* Special case for catch-all arg
    * For example
    * rules.each { |(rule, replacement)| break if result.sub!(rule, replacement) } =>
    * [[:opt, nil]]
    */
    if (!RTEST(param)) {
      param = tracer->catch_all_arg;
      value = tracer->catch_all_arg_val;
    } else {
      // XXX to rb_protect, post MVP
      value = rb_funcall(binding, rb_rg_id_local_variable_get, 1, param);
    }
    arg = rb_rg_vt_coerce(rb_sym2str(param), value, tracer->ecopts);
    memcpy(&args[i], &arg, sizeof(rg_variable_info_t));
    RB_GC_GUARD(param);
    RB_GC_GUARD(argtype);
  }
  // since we access params array members directly
  // we want the VALUE to be guarded
  RB_GC_GUARD(params);
}

static rg_variable_info_t rb_rg_tracepoint_return_value(rb_rg_tracer_t *tracer, VALUE retval)
{
  return rb_rg_vt_coerce(tracer->returnvalue_str, retval, tracer->ecopts);
}
#endif

#ifdef RB_RG_TRACE_BLOCKS
static VALUE rb_rg_block_name(const rb_trace_arg_t *tparg)
{
  return rb_sprintf("block:%"PRIsVALUE":%"PRIsVALUE"", tparg->cfp->iseq->body->location.base_label, rb_iseq_first_lineno(tparg->cfp->iseq));
}
#endif

// These are the main interfaces to query the blacklist / whitelist radix tree. These functions are only ever called during method discovery, meaning:
//
// 1) A Ruby method has not already been included in the blacklisted symbol table
// 2) A Ruby method has not been found in the methodinfo table for the trace context
//
// The result of this lookup determines if a method path (combination of Class#method with support for nesting and other combinations) is white or blacklisted.
// The methodinfo table is an intermediate and very fast jump table that can be queried at runtime with a numeric hash value representative
// of the namespace + method combo WITHOUT incurring expensive coercion of those elements to Ruby string objects for every method call.
//
// That way this radix tree is the defacto source of truth for blacklist and whitelist patterns
// which persists on the tracer instance, with a symbol table as fronting cache for runtime
// lookups without the overhead of String conversion required to query the radix tree directly
// on method dispatch.
//
// We follow .NET syntax, but for Ruby specific syntax (if and when this comes around for docs, we'd recommend the following):
//
// (implied blacklist through no "-" prefix, the default, prefix with "+" to whitelist)
//
// * `Foo` - blacklists anything that starts with `Foo`. such as `Foo::Bar#baz` and `Foo#baz`
// * `Foo#` - blacklists anything that starts with `Foo` AND is a method call. such as `Foo#bar`, but will not match `FooBar#baz`.`
// * `Foo::Bar#` - blacklists anything that starts with `Foo::Bar` AND is a method call. such as `Foo::Bar`, but will not match `Foo::Bar::Other#baz`
// * `Foo::` - blacklists anything that starts with the namespace `Foo` such as `Foo::Bar#baz`, but does not match `Foo#bar`
//
// In Ruby "Class#method" refers to an instance method, our parse can also process "Class.method" for class methods.
//
// We also support a special case for dynamically generated methods only such as `Post#before_create_comments` for which one would add rule `before_create_for_` to catch those and others . The requirement for these as they'd need to start with a lowercase letter in order to be picked up by the parser. The method specific focus is that the class or namespace prefix is not know ahead of time as it's application specific and the most common use case for that is ActiveRecord ORM associations, validations and other callbacks.
//
// Another ruby specific edge case is what we call "singleton methods" - methods specific to the method table of a specifc object, module or class (which is an object too) and has a path / class name prefix that starts with `#<` usually
//
// We adapted this terminology internal to the profiler:
// 
// * `fully qualified method` - `Foo::Bar#baz`
// * `path` - `Foo::Bar` for `Foo::Bar#baz` and `Foo` for `Foo#bar` (represents both namespace and class, or just a class that's not namespaced which is a common case too)
// * `method` - `bar` in `Foo#bar`
//
// Test cases implemented to cover some of the more interesting ones that popped up during testing.

// Determine if a specific string (anything really) is blacklisted. Mostly applicable for any pattern we know is to be ignored that does not explicitly fall into
// fully qualified methods (FULL path), path (class / module path without method) and method (Rails injected callbacks etc.)
// The library frame classification uses this function extensively too.
//
static long rb_rg_blacklisted_string_p(const rax *tree, unsigned char *needle, size_t needle_len)
{
  long blacklisted = RG_BLACKLIST_UNLISTED;
  raxNode *h;
  raxStack ts;
  int splitpos  = 0;

  raxStackInit(&ts);
  /*
   * Walk the tree up to i characters to find a match.
   * i == 0: no match
   * i < needle_size: partial match, traverse tree back up checking whitelist/blacklist flags.
   * i == needle_size: Match, might be a node or a key.
  */
  size_t i = raxLowWalk((rax *)tree, needle, needle_len, &h, NULL, &splitpos, &ts);
  if(i==0)
  {
    // already initialized as unlisted, nothing to do here
  } else
  {
    do
    {
      if(h->iskey)
      {
        blacklisted = (long)raxGetData(h);
        if (blacklisted == RG_BLACKLIST_WHITELISTED) break;
      }
    } while ( (h = raxStackPop(&ts)) );
  }
  raxStackFree(&ts);
  return blacklisted;
}

// Determine if a specific method is blacklisted. It first tries to match fully qualified (class path + method), class path only and eventually explicit method only.
//
static long rb_rg_blacklisted_method_p(const rb_rg_tracer_t *tracer, unsigned char *fully_qualified, size_t fully_qualified_len, unsigned char *path, size_t path_len, unsigned char *method, size_t method_len, int debug)
{
  raxIterator piter;
  int blacklisted = RG_BLACKLIST_UNLISTED;
  void *data = NULL;


  // look for an exact match of the fully qualified method, first
  data = raxFind(tracer->blacklist_fq, fully_qualified, fully_qualified_len);
  if (data != raxNotFound) {
    blacklisted = (long)data;
    if (UNLIKELY(debug)) printf("BL exact fq match on '%s' %d\n", fully_qualified, blacklisted);
    goto matched;
  }

  // look for an exact match of the path, second
  data = raxFind(tracer->blacklist_paths, path, path_len);
  if (data != raxNotFound) {
    blacklisted = (long)data;
    if (UNLIKELY(debug)) printf("BL exact path match on '%s' %d\n", path, blacklisted);
    goto matched;
  }

  // look for an exact match of the method, third
  data = raxFind(tracer->blacklist_methods, method, method_len);
  if (data != raxNotFound) {
    blacklisted = (long)data;
    if (UNLIKELY(debug)) printf("BL exact method match on '%s' %d\n", method, blacklisted);
    goto matched;
  }

  // Path edge cases
  raxStart(&piter, tracer->blacklist_paths);
  raxSeek(&piter, "<=", path, path_len);
  while(raxNext(&piter)) {
    if (strncmp((const char*)piter.key, (const char*)path, 1) > 0) {
      if (UNLIKELY(debug)) printf("BL STOP %.*s '%s': %.*s path iter %lu\n", 1, path, path, (int)piter.key_len, (char*)piter.key, (long)piter.data);
      break;
    }
    if (piter.key_len > 2) {
      // Catch for class paths that terminate with "::" strings
      if (strncmp((const char *)(piter.key + piter.key_len - 2), "::", 2) == 0 && strncmp((const char*)fully_qualified, (const char*)piter.key, piter.key_len) == 0) {
        blacklisted = (long)piter.data;
        if (UNLIKELY(debug)) printf("BL '%s': %.*s path match on tailing '::' %d\n", path, (int)piter.key_len, (char*)piter.key, blacklisted);
        raxStop(&piter);
        goto matched;
      }
      // Catch for method paths that terminate with "#" strings
      if (strncmp((const char *)(piter.key + piter.key_len - 1), "#", 1) == 0 && strncmp((const char*)fully_qualified, (const char*)piter.key, piter.key_len) == 0) {
        blacklisted = (long)piter.data;
        if (UNLIKELY(debug)) printf("BL '%s': %.*s path match on tailing '#' %d\n", path, (int)piter.key_len, (char*)piter.key, blacklisted);
        raxStop(&piter);
        goto matched;
      }
    }
  }
  raxStop(&piter);

  // Paths specific happy path
  blacklisted = rb_rg_blacklisted_string_p(tracer->blacklist_paths, path, path_len);
  if (blacklisted != RG_BLACKLIST_UNLISTED) goto matched;

  // Raw methods
  blacklisted = rb_rg_blacklisted_string_p(tracer->blacklist_methods, method, method_len);

matched:
  if (UNLIKELY(debug)) printf("BL final %s %d\n", fully_qualified, blacklisted);
  return blacklisted;
}

// Helpers for generating class paths. All of the next 3 methods are only called during initial method discovery, isn't cheap, but once off. rb_class_path_cached
// is crucial here as it piggy backs off Ruby's internal class resolution cache, For fall through, rb_class_path would set the path for the next cached lookup.
//
static VALUE rb_rg_class_path(VALUE klass) {
  VALUE cached_path = rb_class_path_cached(klass);
  if (!NIL_P(cached_path)) {
    return cached_path;
  }
  return rb_class_path(klass);
}

// Fetches the singleton object for the given class, if any
static VALUE rb_rg_singleton_object(VALUE singleton_class) {
  return rb_iv_get(singleton_class, "__attached__");
}

// Coerces the given class to a fully qualified path. Works with modules and singleton classes too
static VALUE rb_rg_class_to_str(VALUE klass) {
  while (FL_TEST(klass, FL_SINGLETON)) {
    klass = rb_rg_singleton_object(klass);
    if (!RB_TYPE_P(klass, T_MODULE) && !RB_TYPE_P(klass, T_CLASS)) {
      // singleton of an instance
      klass = rb_obj_class(klass);
    }
  }
  return rb_rg_class_path(klass);
}

// Helper function to populate pointers to class and method Ruby String objects, with awareness of the builtin translator table
inline static void rb_rg_fill_class_and_method(rb_rg_tracer_t *tracer, VALUE namespace, rb_trace_arg_t *tparg, rb_event_flag_t flag, VALUE *class_name, VALUE *method_name)
{
  char *replacement = NULL;
  *class_name = rb_rg_class_to_str(namespace);
  if (st_lookup(tracer->builtin_translator, (st_data_t)StringValueCStr(*class_name), (st_data_t *)&replacement)) {
    *class_name = rb_str_new2(replacement);
  }
#ifdef RB_RG_TRACE_BLOCKS
  if UNLIKELY((flag == RUBY_EVENT_B_CALL)) {
    *method_name = rb_rg_block_name(tparg);
  } else {
#endif
    *method_name = rb_sym2str(rb_tracearg_method_id(tparg));
#ifdef RB_RG_TRACE_BLOCKS
  }
#endif
  RB_GC_GUARD(*class_name);
  RB_GC_GUARD(*method_name);
}

// Fast numeric hash computation of Namespace#method
//
// To revisit to validate how stable method IDs are
// during process lifetime and if we can sidestep hashing
// altogether. Very likely not because of method redefinition.
//
static inline st_index_t rb_rg_method_id_production(VALUE namespace, VALUE method)
{
  st_index_t method_id;
  method_id = rb_hash_start((st_index_t)(namespace));
  method_id = rb_hash_uint(method_id, (st_index_t)method);
  method_id = rb_hash_end(method_id);
  return method_id;
}

// A much slow String based implementation for development environments in Rails which supports code reloading and can introduce drift between
// actual and previously discovered methods in the methodinfo table. String hashes are also a lot more expensive than the numeric ones.
static inline st_index_t rb_rg_method_id_development(rb_rg_tracer_t *tracer, VALUE namespace, rb_trace_arg_t *tparg, rb_event_flag_t flag)
{
  st_index_t method_id;
  VALUE class_name, method_name;
  rb_rg_fill_class_and_method(tracer, namespace, tparg, flag, &class_name, &method_name);
  method_id = rb_hash_start(rb_str_hash(class_name));
  method_id = rb_hash_uint(method_id, rb_str_hash(method_name));
  method_id = rb_hash_end(method_id);
  RB_GC_GUARD(class_name);
  RB_GC_GUARD(method_name);
  return method_id;
}


// Called from the CALL and END handlers and computes the appropriate method hash. Supports both blocks (closures) and method calls and can compute
// IDs for both production and development environments (as is outlined above).
//
static inline st_index_t rb_rg_method_id(rb_rg_tracer_t *tracer, VALUE namespace, rb_trace_arg_t *tparg, rb_event_flag_t flag, rb_event_flag_t method_flag)
{
#ifdef RB_RG_TRACE_BLOCKS
    if (LIKELY(flag == method_flag))
    {
#endif
    RB_GC_GUARD(namespace);
    if (LIKELY(tracer->environment == RB_RG_TRACER_ENV_PRODUCTION)) {
      return rb_rg_method_id_production(namespace, rb_tracearg_method_id(tparg));
    } else {
      return rb_rg_method_id_development(tracer, namespace, tparg, flag);
    }
#ifdef RB_RG_TRACE_BLOCKS
    } else {
      // for blocks class was Qnil when defined without an encapsulating class
      if (NIL_P(namespace)) namespace = rb_cObject;
      if (LIKELY(environment == RB_RG_TRACER_ENV_PRODUCTION)) {
        return rb_rg_method_id_production(namespace, rb_rg_block_id(tparg));
      } else {
        return rb_rg_method_id_development(namespace, tparg, flag);
      }
    }
#endif
}

// Callback function invoked from the Ruby Tracepoint handler when an existing Thread terminates. Causes can be either clean shutdown or an exception raised
// that killed the thread.
//
static void rb_rg_thread_ended(const rb_rg_tracer_t *tracer, VALUE thread)
{
  rg_thread_t *th = rb_rg_thread((rb_rg_tracer_t *)tracer, thread);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] THREAD_ENDED tid: %u\n", th->tid);
#endif
  // Invoke the encoder counterpart to emit this event to the callback sink
  rg_thread_ended(tracer->context, (void *)&tracer->sink_data, th->tid);
  // Free the shadow thread for this Ruby Thread
  xfree(th);
  // Native thread lock around the shared threadsinfo symbol table. Technically it's not needed for this delete operation as threads
  // only ever delete themselves from ths table (the Tracepoint is a "safepoint", so no concurrent execution happens there because of the interpreter lock)
  rb_nativethread_lock_lock((rb_nativethread_lock_t*)&tracer->thread_lock);
  // Removes from the threadsinfo table. Note that the threads counter for TID is monotonically increasing - we never reuse numeric TID mappings as that can
  // lead to all kinds of fail
  st_delete(tracer->threadsinfo, (st_data_t *)&thread, NULL);
  rb_nativethread_lock_unlock((rb_nativethread_lock_t*)&tracer->thread_lock);
  RB_GC_GUARD(thread);
}

// Callback function invoked from the Ruby Tracepoint handler when a new Thread is spawned. The internal VM event naming suggest this is invoked on
// thread creation, but in reality it's when the thread is first scheduled to run by the VM. This difference doesn't matter for this tracing profiler
// as method calls etc. would only be observed on scheduling anyways and as long as the schedule happens before a method call in the thread execution
// context, there's a TID method commands can attach to.
//
static void rb_rg_thread_started(rb_rg_tracer_t *tracer, VALUE parent_thread, VALUE thread)
{
  rg_thread_t *th = NULL;
  rg_thread_t *parent_th = NULL;
  // If there's no explicit parent thread given, assign the current executing thread as the parent thread
  if (NIL_P(parent_thread)) parent_thread = rb_thread_current();
  // Lookup to see if we've seen the currently executing thread in the threadsinfo table before. It may not be true when the Tracer first runs and immediately
  // encounters a new thread without a method call against the main thread for example.
  st_lookup(tracer->threadsinfo, (st_data_t)parent_thread, (st_data_t *)&parent_th);
  // Lock here for safety as we're touching shared state for the symbol table as well as the threads counter (monomitic TID counter)
  rb_nativethread_lock_lock(&tracer->thread_lock);
  // Bumps the TID counter
  tracer->threads++;
  // Allocate and populate a shadow thread for this Ruby Thread
  th = ZALLOC(rg_thread_t);
  th->tid = tracer->threads;
  th->parent_tid = (parent_th ? parent_th->tid : RG_THREAD_ORPHANED);
  th->shadow_top = RG_THREAD_FRAMELESS;
  th->vm_top = RG_THREAD_FRAMELESS;
  // Map the Ruby Thread to the shadow thread
  st_insert(tracer->threadsinfo, (st_data_t)thread, (st_data_t)th);
  rb_nativethread_lock_unlock(&tracer->thread_lock);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] THREAD_STARTED tid: %u\n", th->tid);
#endif
  // Invoke the encoder counterpart to emit this event to the callback sink
  rg_thread_started(tracer->context, (void *)&tracer->sink_data, th);
  RB_GC_GUARD(thread);
}

// Ruby specific wrapper for the process frequency command - mostly just invokes the encoder counterpart.
static void rb_rg_process_frequency(const rb_rg_tracer_t *tracer, rg_frequency_t frequency)
{
  rg_process_frequency(tracer->context, (void *)&tracer->sink_data, 0, frequency);
}

// Invoked when a new Trace is started. Mostly delegates to the encoder specific begin transaction helper
// and sets the API key, technology type and process type fields.
//
static void rb_rg_begin_transaction(const rb_rg_tracer_t *tracer, rg_tid_t tid)
{
  rg_encoded_string_t api_key_string, technology_type_string, process_type_string;
  api_key_string.encoding = RG_STRING_ENCODING_ASCII;
  technology_type_string.encoding = RG_STRING_ENCODING_ASCII;
  process_type_string.encoding = RG_STRING_ENCODING_ASCII;

  // XXX possible to memoize this per trace, but negligible overhead
  rb_rg_encode_string(&api_key_string, tracer->api_key, Qnil);
  rb_rg_encode_string(&technology_type_string, tracer->technology_type, Qnil);
  rb_rg_encode_string(&process_type_string, tracer->process_type, Qnil);
  // Invoke the encoder counterpart to emit this event to the callback sink
  rg_begin_transaction(tracer->context, (void *)&tracer->sink_data, tid, api_key_string, technology_type_string, process_type_string);
}

// Ruby specific wrapper for the end transaction command - mostly just invokes the encoder counterpart.
static void rb_rg_end_transaction(const rb_rg_tracer_t *tracer, rg_tid_t tid)
{
  rg_end_transaction(tracer->context, (void *)&tracer->sink_data, tid);
}

// Invoked during async methodinfo table sync only. Mostly delegates to the encoder specific process type helper
// and sets the technology type and process type fields. Begin transaction mostly replaced this for trace specific contexts.
//
static void rb_rg_process_type(const rb_rg_tracer_t *tracer)
{
  rg_encoded_string_t technology_type_string, process_type_string;
  // XXX pending encoded string support in spec
  technology_type_string.encoding = RG_STRING_ENCODING_ASCII;
  process_type_string.encoding = RG_STRING_ENCODING_ASCII;

  rb_rg_encode_string(&technology_type_string, tracer->technology_type, Qnil);
  rb_rg_encode_string(&process_type_string, tracer->process_type, Qnil);
  rg_process_type(tracer->context, (void *)&tracer->sink_data, 0, technology_type_string, process_type_string);
}

#ifdef RB_RG_EMIT_ARGUMENTS
static void rb_rg_begin(const rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rg_tid_t tid, rg_instance_id_t instance, rg_function_id_t function_id, rg_length_t argc, rg_variable_info_t args[])
{
  rg_begin(tracer->context, (void *)&tracer->sink_data, tid, function_id, instance, argc, args);
}
#else
// Callback function invoked from the Ruby Tracepoint handler when a method call is entered. Mostly delegates to the encoder helper
static void rb_rg_begin(const rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rg_tid_t tid, rg_instance_id_t instance, rg_function_id_t function_id)
{
  rg_begin(tracer->context, (void *)&tracer->sink_data, tid, function_id, instance);
}
#endif

#ifdef RB_RG_EMIT_ARGUMENTS
static void rb_rg_end(const rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rg_tid_t tid, rg_function_id_t function_id, rg_variable_info_t *returnvalue)
#else
// Callback function invoked from the Ruby Tracepoint handler when a method call returns. Mostly delegates to the encoder helper
static void rb_rg_end(const rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rg_tid_t tid, rg_function_id_t function_id, rg_void_return_t *returnvalue)
#endif
{
  rg_end(tracer->context, (void *)&tracer->sink_data, tid, function_id, returnvalue);
}

// The main workorse function for emitting method information to the Raygun APM agent.
// 
// There's 2 data structures that keep (and inform) state to track:
//
// 1) [mostly fixed] The radix tree on the tracer struct (source of truth for black and whitelisted method patterns)
// 2) [mostly fixed] The methodinfo symbol table on the tracer which trakcs both discovered whitelisted and blacklisted methods
//
static rg_method_t *rb_rg_methodinfo(rb_rg_tracer_t *tracer, rb_rg_trace_context_t *trace_context, rg_tid_t tid, VALUE namespace, st_index_t method, rb_event_flag_t flag, rb_trace_arg_t *tparg)
{
  int ret;
  VALUE class_name, method_name, path;
  st_data_t entry;
  rg_encoded_string_t method_name_string, class_name_string;
  rg_method_t *rg_method = NULL;
  // Default to user method source
  rg_method_source_t source = RG_METHOD_SOURCE_USER_CODE;
  // XXX pending encoded string support in spec (methods and classes can be unicode)
  method_name_string.encoding = RG_STRING_ENCODING_ASCII;
  class_name_string.encoding = RG_STRING_ENCODING_ASCII;
  static const char *entrypoint = RG_TRACE_ENTRYPOINT_FRAME_NAME;

  // Needle to lookup blacklist state with - we chose a larger 4kb buffer
  // as it's aligned with max string sizes the agent accepts and generally
  // should be large enough for most use cases, but to revisit.
  unsigned char blacklist_needle[RG_MAX_BLACKLIST_NEEDLE_SIZE];

  // May transition to wait for sync source
  rb_rg_fill_class_and_method(tracer, namespace, tparg, flag, &class_name, &method_name);

  RB_GC_GUARD(namespace);
  RB_GC_GUARD(class_name);
  RB_GC_GUARD(method_name);

  // Precompute the needle size, length of namespace name + "#" + method name + sentinel
  size_t class_name_length = RSTRING_LEN(class_name);
  if (class_name_length >= RG_MAX_STRING_SIZE) class_name_length = RG_MAX_STRING_SIZE;
  size_t method_name_length = RSTRING_LEN(method_name);
  if (method_name_length >= RG_MAX_STRING_SIZE) method_name_length = RG_MAX_STRING_SIZE;
  size_t blacklist_needle_size = class_name_length + 1 + method_name_length + 1;
  snprintf((char *)blacklist_needle, blacklist_needle_size, "%s#%s", StringValueCStr(class_name), StringValueCStr(method_name));

  // Query the blacklist radix tree for the needle above
  if (rb_rg_blacklisted_method_p(tracer, blacklist_needle, blacklist_needle_size - 1, (unsigned char *)StringValueCStr(class_name), class_name_length, (unsigned char *)StringValueCStr(method_name), method_name_length, tracer->debug_blacklist) != RG_BLACKLIST_BLACKLISTED) {
    // This method is not to be blacklisted, add it to the trace context methodinfo table and emit to the agent.
    rb_rg_encode_string(&method_name_string, method_name, Qnil);
    rb_rg_encode_string(&class_name_string, class_name, Qnil);

    // Expensive, but one time during discovery and never called again for this particular method
    path = rb_tracearg_path(tparg);
    RB_GC_GUARD(path);
    // Lock the methodinfo table as a good practice to prevent touching shared state (symbol table and the function ID monotonic counter)
    rb_nativethread_lock_lock(&tracer->method_lock);
    // Another thread already added the same method, early return from
    // the lookup and return the method.
    if (st_lookup(tracer->methodinfo, (st_data_t)method, &entry)){
      rg_method = (rg_method_t *)entry;
      rb_nativethread_lock_unlock(&tracer->method_lock);
      return rg_method;
      // Add the method in a write lock to the symbol table
    } else {
      rg_method = ZALLOC(rg_method_t);
      tracer->methods++;
      rg_method->function_id = tracer->methods;
      rg_method->source = source;
      // Flag synchronization methods (Thread#sleep, mutexes etc.)
      if (st_lookup(tracer->synchronization_methods, (st_data_t)blacklist_needle, NULL)) {
        rg_method->source = RG_METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION;
      }
      // Check if this method is invoked from a known library
      if (rb_rg_blacklisted_string_p(tracer->libraries, (unsigned char*)StringValueCStr(path), RSTRING_LEN(path)) == RG_BLACKLIST_WHITELISTED) {
        if (!rg_method->source) rg_method->source = (rg_method_source_t)RG_METHOD_SOURCE_KNOWN_LIBRARY;
      }

      // Allocate the fully qualified name on the malloc heap as Ruby Strings will be reclaimed by the GC and we don't want to deal with that rabbit hole
      rg_method->name = xmalloc(blacklist_needle_size);
      memcpy(rg_method->name, blacklist_needle, blacklist_needle_size);
      rg_method->length = blacklist_needle_size;

      // only the entrypoint frame (frame 1) is considered a system source frame at present
      if (UNLIKELY(strcmp(entrypoint, StringValueCStr(method_name)) == 0)) {
        rg_method->source = RG_METHOD_SOURCE_SYSTEM;
      }
      // Insert in into the methodinfo table and unlock
      ret = st_insert(tracer->methodinfo, (st_data_t)method, (st_data_t)rg_method);
      // XXX there's a lot going on in this lock - evaluate if we can reduce the locked scope
      rb_nativethread_lock_unlock(&tracer->method_lock);
    }
    // Call the encoder helper
    rg_methodinfo(tracer->context, (void *)&tracer->sink_data, tid, rg_method, class_name_string, method_name_string);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_BLACKLIST)) {
      if (ret == 0) {
        printf("[Raygun APM] whitelisted method ctx: %p tid: %u namespace: %p, method: %lu function_id:%u %s\n", (void *)trace_context, tid, (void *)namespace, method, rg_method->function_id, blacklist_needle);
      } else {
        printf("[Raygun APM] REPLACED whitelisted method ctx: %p tid: %u namespace: %p, method: %lu function_id:%u %s\n", (void *)trace_context, tid, (void *)namespace, method, rg_method->function_id, blacklist_needle);
      }
    }
#endif
    return rg_method;
  } else {
      // Blacklisted method path - also lock as it touches the methodinfo table and the blacklisted methods counter as shared state. Blacklisted methods are inserted
      // into the methodinto table too BUT point to a special blacklisted scalar value instead of a pointer to a rg_method_t struct. This ensures we have 1 source of truth
      // for all method state and ensures 1 symbol table lookup as opposed to having to do multiple if tracked in distinct tables.
      rb_nativethread_lock_lock(&tracer->method_lock);
      tracer->blacklisted++;
      ret = st_insert(tracer->methodinfo, (st_data_t)method, RG_BLACKLIST_BLACKLISTED);
      rb_nativethread_lock_unlock(&tracer->method_lock);
#ifdef RB_RG_DEBUG
      if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_BLACKLIST)) {
        if (ret == 0) {
          printf("[Raygun APM] blacklisted method ctx: %p tid: %u namespace: %p, method: %lu %s\n", (void *)trace_context, tid, (void *)namespace, method, blacklist_needle);
        } else {
          printf("[Raygun APM] REPLACED blacklisted method ctx: %p tid: %u namespace: %p, method: %lu %s\n", (void *)trace_context, tid, (void *)namespace, method, blacklist_needle);
        }
      }
#endif
      // Let the caller know to early return
      return NULL;
  }
}

// Callback function invoked from the Ruby Tracepoint handler when a new exceptino is thrown. Delegates to the wire protocol encoding helper but also
// generates a unique correlation ID for this exception for the raygun4ruby Crash Reporter integration.
//
static void rb_rg_exception_thrown(const rb_rg_tracer_t *tracer, rg_tid_t tid, VALUE exception)
{
  VALUE class_name, correlation_id;
  rg_encoded_string_t class_name_string, correlation_id_string;

  class_name_string.encoding = RG_STRING_ENCODING_ASCII;
  class_name = rb_class_name(CLASS_OF(exception));
  rb_rg_encode_string(&class_name_string, class_name, Qnil);

  correlation_id_string.encoding = RG_STRING_ENCODING_ASCII;
  // Correlation ID is a tuple of [PID, pointer to the tracer, pointer to the exception] - should be unique enough to not have any collission opportunity for
  // the same customer in a 30 day trace retention window.
  correlation_id = rb_sprintf("%d-%lu-%lu", tracer->context->pid, (VALUE)tracer, exception);
  rb_ivar_set(exception, rb_rg_id_exception_correlation_ivar, correlation_id);
  rb_rg_encode_string(&correlation_id_string, correlation_id, Qnil);

  // Call the encoder helper
  rg_exception_thrown(tracer->context, (void *)&tracer->sink_data, tid, (rg_exception_instance_id_t)exception, class_name_string, correlation_id_string);
  RB_GC_GUARD(class_name);
  RB_GC_GUARD(correlation_id);
}

// For the rb_protect call on forcing a graceful shutdown of the sink thread, on tracer shutdown
static VALUE rb_rg_join_sink_thread(VALUE thread)
{
  return rb_funcall(thread, rb_rg_id_join, 1, INT2NUM(RG_SHUTDOWN_GRACE_SECONDS));
}

// Called when the profiler is shutdown. A process can have only 1 profiler instances as per the singleton init pattern used in the wrapper gems, but this
// may not be true for ad hoc uses such as our unit tests. We implemented this as an at_exit handler as a first pass but noticed GC race conditions between
// that callback and the tracer. This method is now called form a GC finalizer set for the tracer instance instead - ensures that it's called while the
// tracer object is still alive.
//
static void rb_rg_process_ended(VALUE obj)
{
  int status = 0;
  rb_rg_get_tracer(obj);
  // First flush any left over bits in the bipbuf ring buffer
  rb_rg_flush_batched_sink(tracer);
  // Let the Agent know we died
  rg_process_ended(tracer->context, (void *)&tracer->sink_data, 0);
  if(tracer->sink_thread)
  {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_DEBUG && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] flushing sinks on exit\n");
#endif
    // Sets the termination condition for the timer thread too.
    tracer->sink_data.running = false;
    // Give it a small grace period (but happy path is almost always immediate) to terminate
    rb_protect(rb_rg_join_sink_thread, tracer->sink_thread, &status);
    if (UNLIKELY(status)) {
      rb_rg_log_silenced_error();
      // Clearing error info to ignore the caught exception
      rb_set_errinfo(Qnil);
    }
  }
}

#ifdef RB_RG_DEBUG_SHADOW_STACK
void rb_rg_print_with_indent(rg_function_id_t function_id, char *direction, int indent)
{
    printf("%*s %d\n", indent * 2, direction, function_id);
}
#endif

// Push a function on the shadow stack of the given shadow thread.
static inline void rb_rg_stack_push(rg_thread_t *thread, rg_function_id_t function)
{
  thread->shadow_stack[++thread->shadow_top] = function;
}

// Pops a function from the shadow stack of the given shadow thread.
static inline rg_function_id_t rb_rg_stack_pop(rg_thread_t *thread)
{
  return thread->shadow_stack[thread->shadow_top--];
}

// Peeks at a function at the top of the shadow stack of the given shadow thread.
static inline rg_function_id_t rb_rg_stack_peek(rg_thread_t *thread)
{
  return thread->shadow_stack[thread->shadow_top];
}

// Callback from the Ruby tracepoint API - try to do as little work as possible here, BUT unfortunately there's a lot going on
// As a future optimization it may make sense to have distinct callbacks per event eg. RUBY_EVENT_THREAD_BEGIN would have it's own
// to reduce code size of the callback and remove branches + the switch statement.
//
static void rb_rg_tracing_hook_i(VALUE tpval, void *data)
{
  VALUE exception, namespace, thread;
  st_data_t entry;
  st_index_t method;
#ifdef RB_RG_EMIT_ARGUMENTS
  int argc, arity;
  rg_variable_info_t args[RG_MAX_ARGS_LENGTH];
  VALUE retval, params, binding;
  rg_variable_info_t return_value;
  // Fixed NULL value return, let it be static so we can init it once
  static rg_variable_info_t return_value;
#else
// Fixed NULL value return, let it be static so we can init it once
   static rg_void_return_t return_value;
#endif
  rg_instance_id_t instance;
  rg_function_id_t function_id;
  rb_rg_trace_context_t *trace_context = (rb_rg_trace_context_t *)data;
  rb_rg_tracer_t *tracer = (rb_rg_tracer_t *)trace_context->tracer;
  rg_method_t *rg_method = NULL;
  rg_thread_t *rg_thread = trace_context->rg_thread;

  // Grab a reference to the current executing thread
  thread = rb_thread_current();

  // We don't care about what happens on the sink or timer threads - a few Ruby method calls occur on those.
  if (UNLIKELY(thread == tracer->sink_thread || thread == tracer->timer_thread)) return;

  // Get the relevant information from the Tracepoint API we need to make decisions on how to proceed further
  rb_trace_arg_t *tparg = rb_tracearg_from_tracepoint(tpval);
  rb_event_flag_t flag = rb_tracearg_event_flag(tparg);

  // Let the trace context's parent thread be the current thread UNLESS we're processing the RUBY_EVENT_THREAD_BEGIN event
  if (UNLIKELY(flag ^ RUBY_EVENT_THREAD_BEGIN)) {
    trace_context->parent_thread = thread;
  }

  // A thread noise filter for traces - we only care about the thread that started the trace (typically a worker thread)
  // OR any threads that has the same Thread Group assigned, meaning they were spawned by the thread that is pinned to the
  // trace context.
  if (UNLIKELY(thread != trace_context->thread)) {
    if (LIKELY(rb_rg_thread_group(GET_THREAD()) == trace_context->thgroup)) {
      // Let tid be that of the current executing thread as it's part of the trace context's thread
      // group and thus it was spawned within the trace context transaction boundaries and thus we
      // care about instrumenting it
      rg_thread = rb_rg_thread(tracer, thread);
    } else {
      // Don't process threads that do not belong to the same group as the trace context's thread. This filters out
      // transient housekeeping threads like connection pool cleanups etc. which just adds noise.
      return;
    }
  }

  switch (flag)
  {
    // Handles method call entry
    case RUBY_EVENT_CALL:
#ifdef RB_RG_TRACE_BLOCKS
    case RUBY_EVENT_B_CALL:
#endif
    {
    // Increments the stack depth of the Ruby VM frames - this can exceed the shadown stack top as we back out after 255 frames deep into a trace
    rg_thread->vm_top++;
    if (UNLIKELY(rg_thread->shadow_top == RG_SHADOW_STACK_LIMIT - 1)) return;

    // Get the namespace from the tracepoint arg
    namespace = rb_tracearg_defined_class(tparg);
    // Excludes the tracer and it's methods
    if (UNLIKELY(namespace == rb_cRaygunTracer)) return;

    // Calculate the numeric method ID for the method being called
    method = rb_rg_method_id(tracer, namespace, tparg, flag, RUBY_EVENT_CALL);
    // Lookup into the method info table to determine if we've already discovered this method and if true, if it's white or blacklisted
    if (LIKELY(st_lookup(tracer->methodinfo, (st_data_t)method, &entry))){
      // Early return if this method is blacklisted
      if ((int)entry == RG_BLACKLIST_BLACKLISTED) return;
      // Cast to a rg_method_t struct otherwise
      rg_method = (rg_method_t *)entry;
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
        printf("[Raygun APM] methodinfo table method ctx: %p tid: %u namespace: %p method: %lu function_id: %u\n", (void *)trace_context, rg_thread->tid, (void *)namespace, method, rg_method->function_id);
#endif
    } else {
      // We haven't seen this method yet, attempt to add it to the methodinfo table. This called fuction determines the white or blacklised status
      // of this method
      rg_method = rb_rg_methodinfo(tracer, trace_context, rg_thread->tid, namespace, method, flag, tparg);
      // A NULL return means the method is blacklisted, let's early return
      if (!rg_method) return;
    }

    // An optimization that only goes 1 level deep into library specific method frames to cleanup traces from uncessary library internals noise
    if (rg_method->source == (rg_method_source_t)RG_METHOD_SOURCE_KNOWN_LIBRARY) {
      rg_thread->level_deep_into_third_party_lib++;
      if (rg_thread->level_deep_into_third_party_lib > 1){
        return;
      }
    }
    //if we are deep into 3rd party libs, do not report sync activity like mutex synchronise, mutex lock / unlock
    //as it will cause method to have children that span for longer than method itself
    if (rg_method->source == (rg_method_source_t)RG_METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION) {
      
      if (rg_thread->level_deep_into_third_party_lib > 1){
        return;
      }
    }

#ifdef RB_RG_EMIT_ARGUMENTS
      arity = rb_mod_method_arity(namespace, tparg->id);
      if (arity != 0) {
        // XXX to rb_protect, post MVP
        params = rb_funcall(tpval, rb_rg_id_parameters, 0, NULL);
        binding = rb_tracearg_binding(tparg);
        rb_rg_tracepoint_parameters(tracer, binding, params, args);
      }
      argc = (arity == 0) ? 0 : (rg_length_t)RARRAY_LEN(params);
      RB_GC_GUARD(params);
#endif

    // Get the object reference on which this method call was invoked
    instance = (rg_instance_id_t)rb_tracearg_self(tparg);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] BEGIN %u ctx: %p tid: %u namespace: %p method: %lu function_id: %u %s#%s\n", rg_method->function_id, (void *)trace_context, rg_thread->tid, (void *)namespace, method, rg_method->function_id, RSTRING_PTR(rb_rg_class_to_str(namespace)), RSTRING_PTR(rb_sym2str(rb_tracearg_method_id(tparg))));
#endif

    // Push this whitelisted method onto the shadow stack
    rb_rg_stack_push(rg_thread, rg_method->function_id);
#ifdef RB_RG_DEBUG_SHADOW_STACK
    rb_rg_print_with_indent(rg_method->function_id, "->", rg_thread->shadow_top);
#endif

#ifdef RB_RG_EMIT_ARGUMENTS
    rb_rg_begin(tracer, trace_context, rg_thread->tid, instance, rg_method->function_id, argc, args);
#else
    // Callback that invokes the encoder and pushes a wire protocol event out to the sink
    rb_rg_begin(tracer, trace_context, rg_thread->tid, instance, rg_method->function_id);
#endif
    }
    break;
    // Handles method call return
    case RUBY_EVENT_RETURN:
#ifdef RB_RG_TRACE_BLOCKS
  case RUBY_EVENT_B_RETURN:
#endif
    {
    // Decrements the stack depth of the Ruby VM frames - this can exceed the shadown stack top as we back out after 255 frames deep into a trace
    rg_thread->vm_top--;
    if (UNLIKELY(rg_thread->vm_top >= RG_SHADOW_STACK_LIMIT - 1)) return;
    if (UNLIKELY(rg_thread->shadow_top == -1)) return;

    // Get the namespace from the tracepoint arg
    namespace = rb_tracearg_defined_class(tparg);
    // Excludes the tracer and it's methods
    if (UNLIKELY(namespace == rb_cRaygunTracer)) return;

    // Calculate the numeric method ID for the method being called
    method = rb_rg_method_id(tracer, namespace, tparg, flag, RUBY_EVENT_RETURN);
    // Lookup into the method info table to determine if we've already discovered this method and if true, if it's white or blacklisted
    st_lookup(tracer->methodinfo, (st_data_t)method, &entry);

    // Early return if this method is blacklisted
    if ((int)entry == RG_BLACKLIST_BLACKLISTED) return;
    // Cast to a rg_method_t struct otherwise
    rg_method = (rg_method_t *)entry;

    // An optimization that only goes 1 level deep into library specific method frames to cleanup traces from uncessary library internals noise
    if (rg_method->source == (rg_method_source_t)(RG_METHOD_SOURCE_KNOWN_LIBRARY)) {
      rg_thread->level_deep_into_third_party_lib--;
      if (rg_thread->level_deep_into_third_party_lib > 0){
        return;
      }
    }
    //if we are deep into 3rd party libs, do not report sync activity like mutex synchronise, mutex lock / unlock
    //as it will cause method to have children that span for longer than method itself
     if (rg_method->source == (rg_method_source_t)RG_METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION) {
      
      if (rg_thread->level_deep_into_third_party_lib > 0){
        return;
      }
    }


    function_id = rg_method->function_id;

    // Pops this whitelisted method from the shadow stack
    rb_rg_stack_pop(rg_thread);

#ifdef RB_RG_EMIT_ARGUMENTS
      retval = rb_tracearg_return_value(tparg);
      return_value = rb_rg_tracepoint_return_value(tracer, retval);
#else
      // static variable "return_value" is initialized in function rb_rg_variable_info_init
      // and passed by reference. Gate multiple useless inits with a type check and the static
      // return value is initialized only once with the init function
      if (UNLIKELY(return_value.type != RG_VT_VOID)) {
        return_value.type = RG_VT_VOID;
        return_value.length = 0;
        return_value.name_length = 0;
      }
#endif

#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] END %u ctx: %p tid: %u namespace: %p method: %lu function_id: %u %s#%s\n", function_id, (void *)trace_context, rg_thread->tid, (void *)namespace, method, function_id, RSTRING_PTR(rb_rg_class_to_str(namespace)), RSTRING_PTR(rb_sym2str(rb_tracearg_method_id(tparg))));
#endif

#ifdef RB_RG_DEBUG_SHADOW_STACK
    rb_rg_print_with_indent(function_id, "<-", rg_thread->shadow_top + 1);
#endif

    // Callback that invokes the encoder and pushes a wire protocol event out to the sink
    rb_rg_end(tracer, trace_context, rg_thread->tid, function_id, &return_value);
  }
  break;
  // Handler for when exceptions are raised
  case RUBY_EVENT_RAISE:
    // Grabs a reference to the exception raised from the tracepoint argument
    exception = rb_tracearg_raised_exception(tparg);
    // ignore the Fatal Raygun exception which shuts the tracer down
    if (UNLIKELY(CLASS_OF(exception) == rb_eRaygunFatal))
      return;

    // Ignore any exceptions on the system entrypoint frame (function ID 1)
    if (rb_rg_stack_peek(rg_thread) == RG_TRACE_ENTRYPOINT_FRAME_ID) return;
    // Ignore any exceptions observed deeper than 1 level deep in library frames (if we only track frames 1 level deep anyays)
    if (rg_thread->level_deep_into_third_party_lib > 0) return;
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_VERBOSE && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST))
      printf("[Raygun APM] EXCEPTION_THROWN tid: %u exc: %p (%s: %s)\n", rg_thread->tid, (void *)exception, RSTRING_PTR(rb_obj_as_string(CLASS_OF(exception))), RSTRING_PTR(rb_obj_as_string(exception)));
#endif
    // Callback that invokes the encoder and pushes a wire protocol event out to the sink
    rb_rg_exception_thrown(tracer, rg_thread->tid, exception);
    break;
  // Handler for when a new thread is first scheduled for execution. We are guaranteed to see this BEFORE any methods is invoked in it's execution context
  case RUBY_EVENT_THREAD_BEGIN:
    // Grabs a reference to the new thread from the tracepoint argument
    thread = rb_tracearg_self(tparg);
    // Callback that invokes the encoder and pushes a wire protocol event out to the sink
    rb_rg_thread_started(tracer, trace_context->parent_thread, thread);
    break;
  // Handler for when a thread terminates
  case RUBY_EVENT_THREAD_END:
    // Grabs a reference to the new thread from the tracepoint argument
    thread = rb_tracearg_self(tparg);
    // Callback that invokes the encoder and pushes a wire protocol event out to the sink
    rb_rg_thread_ended(tracer, thread);
    break;
  }
  RB_GC_GUARD(exception);
  RB_GC_GUARD(namespace);
  RB_GC_GUARD(thread);
}

// Tracer methods

// Called by a GC finalizer (called before the Tracer is collected on program exit) defined in the wrapper gems (Rails and Sidekiq) to signal the
// Agent to stop expecting any streams from this process
//
static VALUE rb_rg_tracer_process_ended(VALUE obj)
{
  rb_rg_get_tracer(obj);
  rb_rg_process_ended(obj);
  return Qnil;
}

// Sets the callback sink which is only used in testing to build up event streams we assert on.
// Expects a Proc that should accept a single argument (the event)
//
static VALUE rb_rg_tracer_callback_sink_set(VALUE obj, VALUE callback)
{
  rb_rg_get_tracer(obj);
  // Validate for a Proc instance
  if (rb_obj_class(callback) != rb_cProc) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected a Proc callback as sink\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected a Proc callback as sink");
  }

  // Validate that the Proc's arity is one argument, otherwise not validating here just results in failure later on and and eventual empty event
  // stream to assert on.
  //
  if (rb_proc_arity(callback) != 1) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected a sink callback that accepts exactly 1 argument\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected a sink callback that accepts exactly 1 argument");
  }

  if (tracer->sink_data.type != RB_RG_TRACER_SINK_NONE)
    rb_raise(rb_eRaygunFatal, "Only one profiler sink can be set!");

  // Inform the encoder context of the Proc aware callback sink
  tracer->context->sink = rb_rg_callback_sink;
  // Set the Proc on sink data - the GC callbacks on the Tracer knows how to handle this properly so the Proc does not get collected before it's
  // not needed anymore
  tracer->sink_data.callback = callback;
  tracer->sink_data.type = RB_RG_TRACER_SINK_CALLBACK;
  return callback;
}

// An intermediate function that is wrapped in rb_protect and responsible for spawning the timer thread
static VALUE rb_rg_tracer_create_timer_thread(VALUE data)
{
  rb_rg_sink_data_t *sink_data = (rb_rg_sink_data_t *)data;
  return rb_thread_create(rb_rg_timer_thread, (void *)sink_data);
}

// Sets the name of the timer thread so that it's visible in GDB debug contexts etc. and easier to reason about which thread is which
static VALUE rb_rg_tracer_timer_thread_set_name(VALUE thread)
{
  return rb_funcall(thread, rb_rg_id_name_equals, 1, rb_str_new2("raygun timer"));
}

// An intermediate function that is wrapped in rb_protect and responsible for spawning the UDP emission thread
static VALUE rb_rg_tracer_create_udp_sink_thread(VALUE data)
{
  rb_rg_sink_data_t *sink_data = (rb_rg_sink_data_t *)data;
  return rb_thread_create(rb_rg_udp_sink_thread, (void *)sink_data);
}

// An intermediate function that is wrapped in rb_protect and responsible for spawning the TCP emission thread
static VALUE rb_rg_tracer_create_tcp_sink_thread(VALUE data)
{
  rb_rg_sink_data_t *sink_data = (rb_rg_sink_data_t *)data;
  return rb_thread_create(rb_rg_tcp_sink_thread, (void *)sink_data);
}

// Sets the name of the UDP sink thread thread so that it's visible in GDB debug contexts etc. and easier to reason about which thread is which
static VALUE rb_rg_tracer_udp_sink_thread_set_name(VALUE thread)
{
  return rb_funcall(thread, rb_rg_id_name_equals, 1, rb_str_new2("raygun udp sink"));
}

// Sets the name of the TCP sink thread thread so that it's visible in GDB debug contexts etc. and easier to reason about which thread is which
static VALUE rb_rg_tracer_tcp_sink_thread_set_name(VALUE thread)
{
  return rb_funcall(thread, rb_rg_id_name_equals, 1, rb_str_new2("raygun tcp sink"));
}

// Enables the UDP sink for the tracer. The current primary production sink and this function also spawns 1 thread:
// * UDP dispatch thread
//
static VALUE rb_rg_tracer_udp_sink_set(int argc, VALUE* argv, VALUE obj)
{
  int status = 0;
  VALUE kwargs, socket, host, port, receive_buffer_size;
  rb_rg_get_tracer(obj);

  // Scans and validates various supported keyword arguments
  rb_scan_args(argc, argv, ":", &kwargs);
  if (NIL_P(kwargs)) kwargs = rb_hash_new();

  socket = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_socket));
  // Assert the socket object passed implements send and connect methods
  if (!(rb_respond_to(socket, rb_rg_id_send) && rb_respond_to(socket, rb_rg_id_connect))){
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected a UDP socket that responds to 'send' and 'connect'\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected a UDP socket that responds to 'send' and 'connect'");
  }

  // Validates the host argument
  host = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_host));
  if (!RB_TYPE_P(host, T_STRING)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP socket hostname to be a string\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the UDP socket hostname to be a string");
  }

  // Validates the port argument
  port = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_port));
  if (!RB_TYPE_P(port, T_FIXNUM)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP socket port to be a numerical value\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the UDP socket port to be a numerical value");
  }

  // Validates the receive buffer size argument. The default receive buffer is calculated by the caller context with this simple pattern:
  // * Initialize the socket
  // * Get the value of the SO_RCVBUF socket option
  // * This value corresponds to the net.rmem_default value on Linux systems
  //
  // We use this value in the jitter buffer implementation on high load dispatch while the bipbuf is still not using much space.
  //
  receive_buffer_size = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_receive_buffer_size));
  if (!RB_TYPE_P(receive_buffer_size, T_FIXNUM)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP receive buffer size to be a numerical value\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the UDP receive buffer size to be a numerical value");
  }

  if (tracer->sink_data.type != RB_RG_TRACER_SINK_NONE)
    rb_raise(rb_eRaygunFatal, "Only one profiler sink can be set!");

  // Inform the encoder to use the batched sink function
  tracer->context->sink = rb_rg_batched_sink;

  // Set the relevant supporting data for this sink on the sink_data member. Integrates properly with the GC.
  tracer->sink_data.tracer = tracer;
  tracer->sink_data.sock = socket;
  tracer->sink_data.host = host;
  tracer->sink_data.port = port;
  tracer->sink_data.receive_buffer_size = NUM2INT(receive_buffer_size);
  // Allocates the ring buffer used for communication between the encoder and the UDP dispatch thread to completely decouple the tracer
  // from the network in the hot path of any other executing thread.
  tracer->sink_data.ringbuf.bipbuf = bipbuf_new(RG_RINGBUF_SIZE);
  if(!tracer->sink_data.ringbuf.bipbuf) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate bipbuf\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate bipbuf");
  }

  // Pre-allocates a Ruby String object of a predefined max packet size and let the GC know we're using it to not have it recycled
  tracer->sink_data.payload = rb_str_buf_new(RG_BATCH_PACKET_SIZE);
  rb_gc_register_address(&tracer->sink_data.payload);
  // Set the sink status to running
  tracer->sink_data.running = true;

  // Spin up the UDP dispatch thread safely - shutdown the tracer if that failed
  tracer->sink_thread = rb_protect(rb_rg_tracer_create_udp_sink_thread, (VALUE)&tracer->sink_data, &status);
  if (UNLIKELY(status)) {
    rb_rg_log_silenced_error();
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Fatal error if we cannot start the UDP sender thread
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not start the UDP sink dispatch thread\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not start the UDP sink dispatch thread");
  }
  // Attempt to set the name for the UDP dispatch thread, no biggy if we can't
  rb_protect(rb_rg_tracer_udp_sink_thread_set_name, tracer->sink_thread, &status);
  if (UNLIKELY(status)) {
    rb_rg_log_silenced_error();
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Not fatal if we cannot set the thread name, continue
  }
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_INFO)) {
      printf("[Raygun APM] UDP dispatch thread started\n");
    }
#endif
  tracer->sink_data.type = RB_RG_TRACER_SINK_UDP;
  return socket;
}

// Enables the TCP sink for the tracer. The current primary production sink and this function also spawns 1 thread:
// * TCP dispatch thread
//
static VALUE rb_rg_tracer_tcp_sink_set(int argc, VALUE* argv, VALUE obj)
{
  int status = 0;
  VALUE kwargs, socket, host, port, receive_buffer_size;
  rb_rg_get_tracer(obj);

  // Scans and validates various supported keyword arguments
  rb_scan_args(argc, argv, ":", &kwargs);
  if (NIL_P(kwargs)) kwargs = rb_hash_new();

  socket = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_socket));
  // Assert the socket object passed implements a send method
  if (!(rb_respond_to(socket, rb_rg_id_send))){
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected a UDP socket that responds to 'send' and 'connect'\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected a TCP socket that responds to 'send'");
  }

  // Validates the host argument
  host = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_host));
  if (!RB_TYPE_P(host, T_STRING)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP socket hostname to be a string\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the TCP socket hostname to be a string");
  }

  // Validates the port argument
  port = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_port));
  if (!RB_TYPE_P(port, T_FIXNUM)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP socket port to be a numerical value\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the TCP socket port to be a numerical value");
  }

  // Validates the receive buffer size argument. The default receive buffer is calculated by the caller context with this simple pattern:
  // * Initialize the socket
  // * Get the value of the SO_RCVBUF socket option
  // * This value corresponds to the net.rmem_default value on Linux systems
  //
  // We use this value in the jitter buffer implementation on high load dispatch while the bipbuf is still not using much space.
  //
  receive_buffer_size = rb_hash_aref(kwargs, ID2SYM(rb_rg_id_receive_buffer_size));
  if (!RB_TYPE_P(receive_buffer_size, T_FIXNUM)) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Expected the UDP receive buffer size to be a numerical value\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Expected the UDP receive buffer size to be a numerical value");
  }

  if (tracer->sink_data.type != RB_RG_TRACER_SINK_NONE)
    rb_raise(rb_eRaygunFatal, "Only one profiler sink can be set!");

  // Inform the encoder to use the bathed sink function
  tracer->context->sink = rb_rg_batched_sink;

  // Set the relevant supporting data for this sink on the sink_data member. Integrates properly with the GC.
  tracer->sink_data.tracer = tracer;
  tracer->sink_data.sock = socket;
  // Set host and port for reconnect (or allow us to support this in the dispatcher thread)
  tracer->sink_data.host = host;
  tracer->sink_data.port = port;
  tracer->sink_data.receive_buffer_size = NUM2INT(receive_buffer_size);
  // Allocates the ring buffer used for communication between the encoder and the TCP dispatch thread to completely decouple the tracer
  // from the network in the hot path of any other executing thread.
  tracer->sink_data.ringbuf.bipbuf = bipbuf_new(RG_RINGBUF_SIZE);
  if(!tracer->sink_data.ringbuf.bipbuf) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate bipbuf\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate bipbuf");
  }

  // Pre-allocates a Ruby String object of a predefined max packet size and let the GC know we're using it to not have it recycled
  tracer->sink_data.payload = rb_str_buf_new(RG_BATCH_PACKET_SIZE);
  rb_gc_register_address(&tracer->sink_data.payload);
  // Set the sink status to running
  tracer->sink_data.running = true;

  // Spin up the UDP dispatch thread safely - shutdown the tracer if that failed
  tracer->sink_thread = rb_protect(rb_rg_tracer_create_tcp_sink_thread, (VALUE)&tracer->sink_data, &status);
  if (UNLIKELY(status)) {
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Fatal error if we cannot start the UDP sender thread
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not start the UDP sink dispatch thread\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not start the TCP sink dispatch thread");
  }
  // Attempt to set the name for the TCP dispatch thread, no biggy if we can't
  rb_protect(rb_rg_tracer_tcp_sink_thread_set_name, tracer->sink_thread, &status);
  if (UNLIKELY(status)) {
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Not fatal if we cannot set the thread name, continue
  }
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_INFO)) {
      printf("[Raygun APM] UDP dispatch thread started\n");
    }
#endif
  tracer->sink_data.type = RB_RG_TRACER_SINK_TCP;
  return socket;
}

// The custom allocator function for the Tracer instance
static VALUE rb_rg_tracer_alloc(VALUE obj)
{
  int status = 0;
  /* allocate the raw tracer struct wrapped by the Ruby object */
  rb_rg_tracer_t *tracer = ZALLOC(rb_rg_tracer_t);
  // Allocate the encoder context - fatal error if this fails
  tracer->context = rg_context_alloc();
  if (!tracer->context) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate the tracer context\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate the tracer context");
  }

  // Default to the production environment (much cheaper method ID hashing)
  tracer->environment = RB_RG_TRACER_ENV_PRODUCTION;
  // Default to no sink set
  tracer->sink_data.type = RB_RG_TRACER_SINK_NONE;
  // Default to not wanting to debug the blacklist
  tracer->debug_blacklist = false;
  // Initializes the symbol table for tracking method info discovered during tracing.
  tracer->methodinfo = st_init_numtable();
  // Allocates the main Radix tree used by the blacklisting implementation - fatal error if this fails
  tracer->blacklist = raxNew();
  if (!tracer->blacklist) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate blacklist radix tree\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate blacklist radix tree");
  }
  // Allocates the fully qualified paths specific Radix tree used by the blacklisting implementation - fatal error if this fails
  tracer->blacklist_fq = raxNew();
  if (!tracer->blacklist_fq) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate blacklist fq radix tree\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate blacklist fq radix tree");
  }
  // Allocates the paths specific Radix tree used by the blacklisting implementation - fatal error if this fails
  tracer->blacklist_paths = raxNew();
  if (!tracer->blacklist_paths) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate blacklist paths radix tree\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate blacklist paths radix tree");
  }
  // Allocates the methods specific Radix tree used by the blacklisting implementation - fatal error if this fails
  tracer->blacklist_methods = raxNew();
  if (!tracer->blacklist_methods) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate blacklist methods radix tree\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate blacklist methods radix tree");
  }
  // Allocates the library classification Radix tree used by the method type classifier - fatal error if this fails
  tracer->libraries = raxNew();
  if (!tracer->libraries) {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate libraries radix tree\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not allocate libraries radix tree");
  }

  // Allocate the symbol table of trace contexts keyed by ThreadGroup (VALUE).
  tracer->tracecontexts = st_init_numtable();

  // For coercion internal function hooks to avoid the overhead of RUBY_EVENT_C_CALL which would absolutely kill tracer performance.
  // Special case and used during method discovery
  tracer->builtin_translator = st_init_strtable_with_size(RB_RG_TRACER_BUILTIN_METHODS_TRANSLATED);
  st_insert(tracer->builtin_translator, (st_data_t)"Raygun::Apm::Hooks::Object", (st_data_t)"Object");
  st_insert(tracer->builtin_translator, (st_data_t)"Raygun::Apm::Hooks::IO", (st_data_t)"IO");
  st_insert(tracer->builtin_translator, (st_data_t)"Raygun::Apm::Hooks::Random", (st_data_t)"Random");
  st_insert(tracer->builtin_translator, (st_data_t)"Raygun::Apm::Hooks::Signal", (st_data_t)"Signal");
  st_insert(tracer->builtin_translator, (st_data_t)"Raygun::Apm::Hooks::Mutex", (st_data_t)"Thread::Mutex");

  // Flags methods that count towards synchronization overhead - special case and used during method discovery
  tracer->synchronization_methods = st_init_strtable_with_size(5);
  st_insert(tracer->synchronization_methods, (st_data_t)"Object#sleep", 0);
  st_insert(tracer->synchronization_methods, (st_data_t)"Thread::Mutex#synchronize", 0);
  st_insert(tracer->synchronization_methods, (st_data_t)"Thread::Mutex#lock", 0);
  st_insert(tracer->synchronization_methods, (st_data_t)"Thread::Mutex#unlock", 0);
  st_insert(tracer->synchronization_methods, (st_data_t)"Thread::Mutex#sleep", 0);

  // Allocates the threads info table for thread (VALUE) => rg_thread_t mappings
  tracer->threadsinfo = st_init_numtable();
  // Monotonically increasing thread_id (tid)
  tracer->threads = 0;
  // Monotonically increasing function id
  tracer->methods = 0;

  // Lock for methodinfo table insert
  rb_nativethread_lock_initialize(&tracer->method_lock);
  // Lock for threads table insert and removal
  rb_nativethread_lock_initialize(&tracer->thread_lock);

  // Default to not logging anything
  tracer->loglevel = RB_RG_TRACER_LOG_NONE;
#ifdef RB_RG_EMIT_ARGUMENTS
  tracer->returnvalue_str = rb_str_new2("returnValue");
  // don't GC the return value variable name string
  // it is critical that rb_gc_register_address gets called right after alloc and before any other allocs
  rb_gc_register_address(&tracer->returnvalue_str);
  tracer->catch_all_arg = ID2SYM(rb_rg_id_catch_all);
  // don't GC the catch all argument name
  // it is critical that rb_gc_register_address gets called right after alloc and before any other allocs
  rb_gc_register_address(&tracer->catch_all_arg);
  tracer->catch_all_arg_val = rb_str_new2("*");
  // don't GC the catch all argument value
  // it is critical that rb_gc_register_address gets called right after alloc and before any other allocs
  rb_gc_register_address(&tracer->catch_all_arg_val);
  // it is critical that rb_gc_register_address gets called right after alloc and before any other allocs
#endif
  // Let the GC know to not try to recycle the options hash used for String encoding
  rb_gc_register_address(&tracer->ecopts);
  tracer->ecopts = rb_hash_new();
  rb_hash_aset(tracer->ecopts, ID2SYM(rb_rg_id_invalid), ID2SYM(rb_rg_id_replace));
  rb_obj_freeze(tracer->ecopts);
  // Pre-allocs and marks the technology type string as in use for the GC
  tracer->technology_type = rb_str_new2("Ruby");
  rb_gc_register_address(&tracer->technology_type);
  // Pre-allocs and marks the process type string as in use for the GC
  tracer->process_type = rb_str_new2("Standalone");
  rb_gc_register_address(&tracer->process_type);
  // Default to no Api Key (pased along with BEGIN_TRANSACTION if present)
  tracer->api_key = Qnil;

  // Sink data shared with timer and specific async dispatch threads (currently only UDP)
  tracer->sink_data.tracer = tracer;
  tracer->sink_data.callback = Qnil;
  tracer->sink_data.sock = Qnil;
  tracer->sink_data.host = Qnil;
  tracer->sink_data.port = Qnil;
  // Initialize the batch struct reused for dispatch
  tracer->sink_data.batch.type = RG_EVENT_BATCH;
  tracer->sink_data.batch.length = RG_BATCH_HEADLEN;
  // Preset the PID of the batch struct from the encoder context - it's not going to change moving forward
  tracer->sink_data.batch.pid = tracer->context->pid;

  // Set a running default state - very important for the timer thread we're about to spawn
  tracer->sink_data.running = true;
  // Default to none-noop mode (for when the minimum required Agent version is not matched)
  tracer->noop = false;

#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Tracer allocated\n");
    }
#endif

  // Spawn the timer thread in a safe manner with rb_protect - fatal error if we couldn't
  tracer->timer_thread = rb_protect(rb_rg_tracer_create_timer_thread, (VALUE)&tracer->sink_data, &status);
  if (UNLIKELY(status)) {
    rb_rg_log_silenced_error();
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Fatal error if we cannot start the timer thread
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not start the timer thread\n");
    }
#endif
    rb_raise(rb_eRaygunFatal, "Could not start the timer thread");
  }

  // Attempt to set the timer thread name - no biggy if this fails
  rb_protect(rb_rg_tracer_timer_thread_set_name, tracer->timer_thread, &status);
  if (UNLIKELY(status)) {
    rb_rg_log_silenced_error();
    // Clearing error info to ignore the caught exception
    rb_set_errinfo(Qnil);
    // Not fatal if we cannot set the thread name, continue
  }
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] timer thread started\n");
    }
#endif
  // Returns the wrapped Ruby object
  return TypedData_Wrap_Struct(obj, &rb_rg_tracer_type, tracer);
}

// A common function and the only interface for adding to the various radix trees used for blacklisting
static VALUE rb_rg_tracer_blacklist_add0(VALUE obj, VALUE path, VALUE method, long data, const char *error_msg, const char *scope)
{
  rb_rg_get_tracer(obj);
  VALUE args[2];
  int res;
  VALUE needle = Qnil;
  // Return if we don't have path of method set
  if (!RTEST(path) && !RTEST(method)) return Qfalse;
  // If both path and method is set, add it to the radix tree that tracks the fully qualified paths rules
  if (RTEST(path) && RTEST(method)) {
    args[0] = path;
    args[1] = method;
    needle = rb_str_format(2, args, rb_str_new2("%s#%s"));
    res = raxInsert(tracer->blacklist_fq, (unsigned char*)StringValueCStr(needle), RSTRING_LEN(needle), (void *)data, NULL);
    if (res == 0 && errno == ENOMEM )
    {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] %s\n", error_msg);
    }
#endif
      rb_raise(rb_eRaygunFatal, "%s", error_msg);
    }
  // If only path is set and method is not, add it to the radix tree that tracks the paths rules
  } else if (RTEST(path) && !RTEST(method)) {
    needle = path;
    res = raxInsert(tracer->blacklist_paths, (unsigned char*)StringValueCStr(needle), RSTRING_LEN(needle), (void *)data, NULL);
    if (res == 0 && errno == ENOMEM )
    {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] %s\n", error_msg);
    }
#endif
      rb_raise(rb_eRaygunFatal, "%s", error_msg);
    }
  // If only method is set and path is not, add it to the radix tree that tracks the methods rules
  } else if (RTEST(method) && !RTEST(path)) {
    needle = method;
    res = raxInsert(tracer->blacklist_methods, (unsigned char*)StringValueCStr(needle), RSTRING_LEN(needle), (void *)data, NULL);
    if (res == 0 && errno == ENOMEM )
    {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] %s\n", error_msg);
    }
#endif
      rb_raise(rb_eRaygunFatal, "%s", error_msg);
    }
  }
  // Fall through - set in the all-of-the-things radix tree
  res = raxInsert(tracer->blacklist, (unsigned char*)StringValueCStr(needle), RSTRING_LEN(needle), (void *)data, NULL);
  if (res == 0 && errno == ENOMEM )
  {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] %s\n", error_msg);
    }
#endif
    rb_raise(rb_eRaygunFatal, "%s", error_msg);
  }
#ifdef RB_RG_DEBUG
  if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_BLACKLIST))
    printf("[Raygun APM] %s pattern %s\n", scope, RSTRING_PTR(needle));
#endif
  // Resets the methodinfo table as the classification rules changed and to reduce complexity and bug surface potential, let the method discovery
  // mechanism in the tracepoint callback just rebuild it.
  rb_rg_flush_caches(tracer);
  RB_GC_GUARD(needle);
  return Qtrue;
}

// Adds a blacklist rule
static VALUE rb_rg_tracer_add_blacklist(VALUE obj, VALUE path, VALUE method)
{
  return rb_rg_tracer_blacklist_add0(obj, path, method, RG_BLACKLIST_BLACKLISTED, "Could not allocate room for blacklisted entry", "blacklisted");
}

// Adds a whitelist rule
static VALUE rb_rg_tracer_add_whitelist(VALUE obj, VALUE path, VALUE method)
{
  return rb_rg_tracer_blacklist_add0(obj, path, method, RG_BLACKLIST_WHITELISTED, "Could not allocate room for whitelisted entry", "whitelisted");
}

// Dumps the rax radix tree to STDOUT if the log level is Blacklist (can be VERY noisy)
static VALUE rb_rg_tracer_show_filters(VALUE obj)
{
  rb_rg_get_tracer(obj);
  raxShow(tracer->blacklist);
  printf("----- BL fully qualified -----\n");
  raxShow(tracer->blacklist_fq);
  printf("----- BL paths -----\n");
  raxShow(tracer->blacklist_paths);
  printf("----- BL methods -----\n");
  raxShow(tracer->blacklist_methods);
  return Qnil;
}

// Helper predicate methods for tests to infer if a given fully qualified method, a path or just a method is considered whitelisted
static VALUE rb_rg_tracer_blacklisted_p(VALUE obj, VALUE fully_qualified, VALUE path, VALUE method)
{
  int res = 0;
  rb_rg_get_tracer(obj);
  RB_GC_GUARD(fully_qualified);
  RB_GC_GUARD(path);
  RB_GC_GUARD(method);
  res = rb_rg_blacklisted_method_p(tracer, (unsigned char*)StringValueCStr(fully_qualified), RSTRING_LEN(fully_qualified), (unsigned char*)StringValueCStr(path), RSTRING_LEN(path), (unsigned char*)StringValueCStr(method), RSTRING_LEN(method), true);
  return (res == RG_BLACKLIST_BLACKLISTED) ? Qtrue : Qfalse;
}

// Helper predicate methods for tests to infer if a given fully qualified method, a path or just a method is considered blacklisted
static VALUE rb_rg_tracer_whitelisted_p(VALUE obj, VALUE fully_qualified, VALUE path, VALUE method)
{
  int res = 0;
  rb_rg_get_tracer(obj);
  RB_GC_GUARD(fully_qualified);
  RB_GC_GUARD(path);
  RB_GC_GUARD(method);
  res = rb_rg_blacklisted_method_p(tracer, (unsigned char*)StringValueCStr(fully_qualified), RSTRING_LEN(fully_qualified), (unsigned char*)StringValueCStr(path), RSTRING_LEN(path), (unsigned char*)StringValueCStr(method), RSTRING_LEN(method), true);
  return (res == RG_BLACKLIST_WHITELISTED || res == RG_BLACKLIST_UNLISTED) ? Qtrue : Qfalse;
}

// Popoulates the radix tree used for library classification with a set of paths known to be prefixes of library specific code
static VALUE rb_rg_tracer_register_libraries(VALUE obj, VALUE libraries)
{
  rb_rg_get_tracer(obj);
  int i, res;

  for (i=0; i < RARRAY_LEN(libraries); i++) {
    VALUE library = RARRAY_AREF(libraries, i);
    res = raxInsert(tracer->libraries, (unsigned char*)StringValueCStr(library), RSTRING_LEN(library), (void *)RG_BLACKLIST_WHITELISTED, NULL);

    if (res == 0 && errno == ENOMEM )
    {
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_ERROR && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Could not allocate room for library entry\n");
    }
#endif
      rb_raise(rb_eRaygunFatal, "Could not allocate room for library entry");
    }
  }
  return Qtrue;
}

// Iterator callback invoked from the trace contexts symbol table to ensure all tracepoints still enabled are disabled.
// This is called on tracer shutdown.
static int rb_rg_trace_context_disable_tracepoint_i(st_data_t key, st_data_t val, st_data_t data)
{
  rb_rg_trace_context_t *trace_context =  (rb_rg_trace_context_t *)val;

  if (RTEST(trace_context->tracepoint)) {
    if (rb_tracepoint_enabled_p(trace_context->tracepoint)) {
        rb_tracepoint_disable(trace_context->tracepoint);
        trace_context->tracepoint = Qnil;
    }
  }
  return ST_DELETE;
}

// Force the shutdown of any tracepoints still active in the trace contexts symbol table.
// Invoked on tracer shutdown, typically when the process exits.
static VALUE rb_rg_tracer_disable_tracepoints(VALUE obj) {
  rb_rg_get_tracer(obj);
  st_foreach(tracer->tracecontexts, rb_rg_trace_context_disable_tracepoint_i, 0);
  return Qtrue;
}

// Sets the log level for the profiler
static VALUE rb_rg_tracer_log_level_equals(VALUE obj, VALUE level)
{
  rg_byte_t loglevel;
  rb_rg_get_tracer(obj);

  Check_Type(level, T_FIXNUM);
  loglevel = (rg_byte_t)NUM2INT(level);
  // Raises argument error if we don't konw about this log level
  if (loglevel < RB_RG_TRACER_LOG_NONE || loglevel > RB_RG_TRACER_LOG_BLACKLIST) {
    rb_raise(rb_eArgError, "invalid log level");
  }
  tracer->loglevel = loglevel;
  return Qtrue;
}

// Sets the environment for the tracer (production or development)
static VALUE rb_rg_tracer_environment_equals(VALUE obj, VALUE environment)
{
  rg_byte_t env;
  rb_rg_get_tracer(obj);

  Check_Type(environment, T_FIXNUM);
  env = (rg_byte_t)NUM2INT(environment);
  // Raises argument error if we don't konw about this environment
  if (env < RB_RG_TRACER_ENV_DEVELOPMENT || env > RB_RG_TRACER_ENV_PRODUCTION) {
    rb_raise(rb_eArgError, "invalid environment");
  }
  tracer->environment = env;
  return Qtrue;
}

// Enables or disables blacklist debugging (for tracer developers only, useless to anyone else)
static VALUE rb_rg_tracer_debug_blacklist_equals(VALUE obj, VALUE debug)
{
  rg_byte_t debug_blacklist;
  rb_rg_get_tracer(obj);

  debug_blacklist = RTEST(debug) ? true : false;
  tracer->debug_blacklist = debug_blacklist;
  return Qtrue;
}

// Sets the API Key for this tracer instance (included in BEGIN_TRANSACTION commmands in a field if set)
static VALUE rb_rg_tracer_api_key_equals(VALUE obj, VALUE api_key)
{
  rb_rg_get_tracer(obj);
  Check_Type(api_key, T_STRING);
  tracer->api_key = api_key;
  rb_gc_register_address(&tracer->api_key);
  return Qtrue;
}

// XXX not exposed from Ruby at present but should be, renamed to no clashed if eventually exposed from MRI core
VALUE rb_rg_thread_group(rb_thread_t *th)
{
  VALUE thgroup;
  thgroup = th->thgroup;
  if (thgroup == 0){
    return Qnil;
  } else {
    return thgroup;
  }
}

VALUE rb_rg_thread_group_add(VALUE thgroup, rb_thread_t *th)
{
  th->thgroup = thgroup;
  return Qnil;
}

// Start a trace context. Could be a single script/console application that has start+stop
// wrapped around or could be a web request. Initializes any per trace context.
//
static VALUE rb_rg_tracer_start_trace(VALUE obj)
{
  rb_event_flag_t events;
  rb_rg_get_tracer(obj);
  rb_rg_get_current_thread_trace_context();

  // If the tracer is in noop (silent) mode, do nothing - this would only be true if the minimum inferred Agent version is not met
  if (UNLIKELY(tracer->noop)) return Qfalse;

  // If no context for the current thread group, register it.
  if (!trace_context)
  {
    // The tracepoint API events we're interested in
    events = RUBY_EVENT_THREAD_BEGIN | RUBY_EVENT_THREAD_END | RUBY_EVENT_RAISE | RUBY_EVENT_CALL | RUBY_EVENT_RETURN;
#ifdef RB_RG_TRACE_BLOCKS
  events |= RUBY_EVENT_B_RETURN | RUBY_EVENT_B_CALL;
#endif

    // Allocates the trace context used for this trace
    trace_context = rb_rg_trace_context_alloc(tracer, thread);


    // XXX ruby c api does not expose the ThreadGroup api so have to go through Ruby land, unfortunately.
    // Add trace main (invoking) thread to a thread group so we could identify spawned child threads.
    trace_context->thgroup = rb_obj_alloc(rb_rg_cThGroup);
    rb_rg_thread_group_add(trace_context->thgroup, current_thread);
    // Insert into the trace contexts table, keyed by thread group
    st_insert(tracer->tracecontexts, (st_data_t)trace_context->thgroup, (st_data_t)trace_context);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] THREAD GROUP allocated for context %p thread: %ld thgroup: %ld\n", (void *)trace_context, thread, trace_context->thgroup);
    }
#endif

    // Emit with each trace context even though startup specific.
    // For example can result in a disconnect between profiler and agent state
    // depending on who comes up first.
    rb_rg_process_frequency(tracer, (rg_frequency_t)TIMESTAMP_UNITS_PER_SECOND);
    rb_rg_begin_transaction(tracer, trace_context->rg_thread->tid);

    // Start a tracepoint specifically for this trace context. Doing so ONLY during actual trace execution removes excess idle / discarded anyways
    // overhead from running tracepoints in contexts we don't care about anyways.
    trace_context->tracepoint = rb_tracepoint_new(Qnil, events, rb_rg_tracing_hook_i, (void *)trace_context);
    // Immediately enable the tracepoint as well
    rb_tracepoint_enable(trace_context->tracepoint);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Trace STARTED for context %p thread: %ld thgroup: %ld\n", (void *)trace_context, thread, trace_context->thgroup);
    }
#endif
    return Qtrue;
  } else
  {
    // If context exists, this is a start inside a start, just short-circuit.
    return Qfalse;
  }
}

// Called when we're done with instrumenting a section of code or unit of work
static VALUE rb_rg_tracer_end_trace(VALUE obj)
{
    rb_rg_get_tracer(obj);
    rb_rg_get_current_thread_trace_context();
    if(trace_context)
    {
      // Emit the END_TRANSACTION command via the encoder
      rb_rg_end_transaction(tracer, trace_context->rg_thread->tid);
      // XXX delete before free on purpose to avoid races on st_lookup
      st_delete(tracer->tracecontexts, (st_data_t *)&thgroup, NULL);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Trace ENDED for context %p\n", (void *)trace_context);
    }
#endif
      // Frees the trace context
      rb_rg_trace_context_free(trace_context);
      return Qtrue;
    } else
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel >= RB_RG_TRACER_LOG_INFO && tracer->loglevel < RB_RG_TRACER_LOG_BLACKLIST)) {
      printf("[Raygun APM] Trace context NOT ENDED - trace context nullified, thread: %ld thgroup: %ld\n", thread, thgroup);
    }
#endif
    {
      return Qfalse;
    }
}

// A helper method that returns the current timestampt by using the same timestamper the encoder uses - called by extended events in Ruby land
static VALUE rb_rg_tracer_now(VALUE obj)
{
  rb_rg_get_tracer(obj);
  return LL2NUM(tracer->context->timestamper());
}

// Emits a given extended event constructed in Ruby land via the encoder sink interface
static VALUE rb_rg_tracer_emit(VALUE obj, VALUE evt)
{
  VALUE encoded;
  rb_rg_get_tracer(obj);
  // Noop extended event emission too which can fire through external notification frameworks like ActiveSupport::Notifications
  if (UNLIKELY(tracer->noop)) return Qfalse;

  // Expect a Raygun::Apm::Event instance
  if (!rb_obj_is_kind_of(evt, rb_cRaygunEvent)) rb_raise(rb_eRaygunFatal, "Invalid extended event - cannot decode");
  rb_rg_get_event(evt);
  encoded = rb_rg_event_encoded(evt);
  memcpy(tracer->context->buf, RSTRING_PTR(encoded), RSTRING_LEN(encoded));
  tracer->context->sink(tracer->context, (void *)&tracer->sink_data, event, (const rg_length_t)RSTRING_LEN(encoded));
  RB_GC_GUARD(encoded);
#ifdef RB_RG_DEBUG
    if (UNLIKELY(tracer->loglevel == RB_RG_TRACER_LOG_INFO)) {
      printf("[Raygun APM] Emitted extended event %s\n", rb_rg_event_type_to_str(event));
    }
#endif
  return Qtrue;
}

// Diagnostics specific (when PROTON_DIAGNOSTICS env var is set) - dumps out the whitelisted methods discovered thus far
static int rb_rg_methodinfo_table_dump_i(st_data_t key, st_data_t val, st_data_t data)
{
  rg_method_t *rg_method = (rg_method_t *)val;
  if ((int)val != RG_BLACKLIST_BLACKLISTED) {
    printf("[WL] %lu %s -> %u\n", key, rg_method->name, rg_method->function_id);
  }
  return ST_CONTINUE;
}

// Diagnostics specific (when PROTON_DIAGNOSTICS env var is set) - dumps out the threads and their ancestry discover thus far
static int rb_rg_threadsinfo_table_dump_i(st_data_t key, st_data_t val, st_data_t data)
{
  rg_thread_t *rg_thread = (rg_thread_t *)val;
  printf("[TH] %lu parent %d -> %d\n", key, rg_thread->parent_tid, rg_thread->tid);
  return ST_CONTINUE;
}

// Diagnostics specific (when PROTON_DIAGNOSTICS env var is set) - dumps out the trace contexts currently in flight (can be multiple under high concurrency)
static int rb_rg_tracecontexts_dump_i(st_data_t key, st_data_t val, st_data_t data)
{
  rb_rg_trace_context_t *rg_trace_context = (rb_rg_trace_context_t *)val;
  printf("[TC] %ld trace_context: %p thread %lu -> %lu (enabled: %lu)\n", key, (void *)rg_trace_context, rg_trace_context->thread, rg_trace_context->tracepoint, rb_tracepoint_enabled_p(rg_trace_context->tracepoint));
  return ST_CONTINUE;
}

// Diagnostics specific (when PROTON_DIAGNOSTICS env var is set) - dumps out a while bunch of info. Not very useful directly for end users as it exposes
// too many internals, but useful for a support request as a back pocket solution + local debugging as well
//
static VALUE rb_rg_tracer_diagnostics(VALUE obj)
{
  rb_rg_get_tracer(obj);
  VALUE thread = rb_thread_current();
  rg_thread_t *th = rb_rg_thread(tracer, thread);
  printf("#### APM Tracer PID %d obj: %p size: %lu bytes\n", tracer->context->pid, (void *)obj, rb_rg_tracer_size(tracer));
  printf("Methods: %d threads: %d nooped: %d\n", tracer->methods, tracer->threads, tracer->noop);
  printf("[Pointers] encoder context: %p threadsinfo: %p methodinfo: %p sink_data: %p batch: %p bipbuf: %p\n", (void *)tracer->context, (void *)tracer->threadsinfo, (void *)tracer->methodinfo, (void *)&tracer->sink_data, (void *)&tracer->sink_data.batch, (void *)tracer->sink_data.ringbuf.bipbuf);
  printf("[Execution context] Raygun thread: %d Ruby current thread: %p thread group: %ld\n", th->tid, (void *)thread, rb_rg_thread_group(GET_THREAD()));
  printf("[Ruby threads] timer thread: %p sink thread: %p\n", (void *)tracer->timer_thread, (void *)tracer->sink_thread);
  if (NIL_P(tracer->sink_data.payload)) {
    printf("[Encoder] batched: %lu raw: %lu flushed: %lu resets: %lu batches: %lu\n", tracer->sink_data.encoded_batched, tracer->sink_data.encoded_raw, tracer->sink_data.flushed, tracer->sink_data.resets, tracer->sink_data.batches);
    printf("[Dispatch] batch count: %d sequence: %d batch pid: %d sink running: %d bytes sent: %lu failed sends: %lu jittered_sends: %lu\n", tracer->sink_data.batch.count, tracer->sink_data.batch.length, tracer->sink_data.batch.pid, tracer->sink_data.running, tracer->sink_data.bytes_sent, tracer->sink_data.failed_sends, tracer->sink_data.jittered_sends);
    printf("[Buffer] size: %d max used: %lu used: %d unused: %d\n", bipbuf_size(tracer->sink_data.ringbuf.bipbuf), tracer->sink_data.max_buf_used, bipbuf_used(tracer->sink_data.ringbuf.bipbuf), bipbuf_unused(tracer->sink_data.ringbuf.bipbuf));
  }
  printf("#### Method table:\n");
  st_foreach(tracer->methodinfo, rb_rg_methodinfo_table_dump_i, 0);
  printf("#### Threads:\n");
  st_foreach(tracer->threadsinfo, rb_rg_threadsinfo_table_dump_i, 0);
  printf("#### Trace contexts:\n");
  st_foreach(tracer->tracecontexts, rb_rg_tracecontexts_dump_i, 0);
  return Qnil;
}

// Helper method for extended events called form Ruby land to ensure a correct mapping of Ruby Thread => wire protocol TID
static VALUE rb_rg_get_thread_id(VALUE obj, VALUE thread)
{
  rg_thread_t *th = NULL;
  rb_rg_get_tracer(obj);
  th = rb_rg_thread(tracer, thread);
  return ULONG2NUM(th->tid);
}

// Set the tracer to noop mode (minimum Agent version not met)
static VALUE rb_rg_tracer_noop_bang(VALUE obj)
{
  rb_rg_get_tracer(obj);
  tracer->noop = true;
  return Qtrue;
}

// For tests only, since https://github.com/ruby/ruby/pull/2638 no public API to retrieve memory address of an object
static VALUE rb_rg_tracer_memory_address(VALUE tracer, VALUE obj)
{
  return ULL2NUM(obj);
}

// The main interface for mapping Ruby Threads to shadow threads
rg_thread_t *rb_rg_thread(rb_rg_tracer_t *tracer, VALUE thread)
{
  rg_thread_t *th = NULL;
  RB_GC_GUARD(thread);
  // Happy path - we're aware of this thread already
  if (st_lookup(tracer->threadsinfo, (st_data_t)thread, (st_data_t *)&th))
  {
    return (rg_thread_t *)th;
  }
  else
  {
    // Register unknown/new threads lazily as we observe them
    rb_nativethread_lock_lock(&tracer->thread_lock);

    tracer->threads++;
    th = ZALLOC(rg_thread_t);
    th->tid = tracer->threads;
/*
  I'm wondering about the scenario where the profiler observers a spurious thread for the first time in a trace, one that likely
  started on framework or application boot and wakes up at X intervals doing X work. We did not explicitly see it coming to life
  during a trace, but observed it running during one.

  Should such a special case thread that we just observe during it's runtime WITHOUT any context on how it came to life or which thread owns it have a parent that is:
  - Orphaned (TID -1)
  - The current thread in which the trace is running

  The latter case seems like it could result in rendering incorrect behavior and is speculative
  How would the parent of this thread be "computed" with the .NET profiler?
*/
    th->parent_tid = RG_THREAD_ORPHANED;
    th->shadow_top = RG_THREAD_FRAMELESS;
    th->vm_top = RG_THREAD_FRAMELESS;
    st_insert(tracer->threadsinfo, (st_data_t)thread, (st_data_t)th);
    rb_nativethread_lock_unlock(&tracer->thread_lock);
    return th;
  }
}

// Ruby API initializer
void _init_raygun_tracer()
{
  // Warms up symbols used in this tracer module
  rb_rg_id_send = rb_intern("send");
  rb_rg_id_name_equals = rb_intern("name=");
  rb_rg_id_connect = rb_intern("connect");
#ifdef RB_RG_EMIT_ARGUMENTS
  rb_rg_id_parameters = rb_intern("parameters");
  rb_rg_id_local_variable_get = rb_intern("local_variable_get");
  rb_rg_id_catch_all = rb_intern("catch_all");
#endif
  rb_rg_id_invalid = rb_intern("invalid");
  rb_rg_id_replace = rb_intern("replace");
  rb_rg_id_config = rb_intern("config");
  rb_rg_id_loglevel = rb_intern("loglevel");
  rb_rg_id_th_group = rb_intern("ThreadGroup");
  rb_rg_id_join = rb_intern("join");
  rb_rg_id_socket = rb_intern("socket");
  rb_rg_id_host = rb_intern("host");
  rb_rg_id_port = rb_intern("port");
  rb_rg_id_receive_buffer_size = rb_intern("receive_buffer_size");
  rb_rg_id_exception_correlation_ivar = rb_intern("@__raygun_correlation_id");
  rb_rg_id_message = rb_intern("message");
  rb_rg_id_write = rb_intern("write");

  // do the thread group class name lookup ahead of time so we don't incur runtime overhead for this
  rb_rg_cThGroup = rb_const_get(rb_cObject, rb_rg_id_th_group);

  // Defines the tracer instance which everything else attaches to
  rb_cRaygunTracer = rb_define_class_under(rb_mRaygunApm, "Tracer", rb_cObject);

#define rg_tracer_const(name, val) \
  rb_define_const(rb_cRaygunTracer, name, INT2NUM(val));

  // Define environment specific constants
  rg_tracer_const("ENV_DEVELOPMENT", RB_RG_TRACER_ENV_DEVELOPMENT);
  rg_tracer_const("ENV_PRODUCTION", RB_RG_TRACER_ENV_PRODUCTION);

  // Define log level specific constants
  rg_tracer_const("LOG_NONE", RB_RG_TRACER_LOG_NONE);
  rg_tracer_const("LOG_INFO", RB_RG_TRACER_LOG_INFO);
  rg_tracer_const("LOG_WARNING", RB_RG_TRACER_LOG_WARNING);
  rg_tracer_const("LOG_ERROR", RB_RG_TRACER_LOG_ERROR);
  rg_tracer_const("LOG_VERBOSE", RB_RG_TRACER_LOG_VERBOSE);
  rg_tracer_const("LOG_DEBUG", RB_RG_TRACER_LOG_DEBUG);
  rg_tracer_const("LOG_EVERYTHING", RB_RG_TRACER_LOG_EVERYTHING);
  rg_tracer_const("LOG_BLACKLIST", RB_RG_TRACER_LOG_BLACKLIST);

  // Define method source specific constants (used in tests only)
  rg_tracer_const("METHOD_SOURCE_USER_CODE", RG_METHOD_SOURCE_USER_CODE);
  rg_tracer_const("METHOD_SOURCE_SYSTEM", RG_METHOD_SOURCE_SYSTEM);
  rg_tracer_const("METHOD_SOURCE_KNOWN_LIBRARY", RG_METHOD_SOURCE_KNOWN_LIBRARY);
  rg_tracer_const("METHOD_SOURCE_WAIT_FOR_USER_INPUT", RG_METHOD_SOURCE_WAIT_FOR_USER_INPUT);
  rg_tracer_const("METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION", RG_METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION);
  rg_tracer_const("METHOD_SOURCE_JIT_COMPILATION", RG_METHOD_SOURCE_JIT_COMPILATION);
  rg_tracer_const("METHOD_SOURCE_GARBAGE_COLLECTION", RG_METHOD_SOURCE_GARBAGE_COLLECTION);

  // For network transports
  rg_tracer_const("BATCH_PACKET_SIZE", RG_BATCH_PACKET_SIZE);

  // Sinks
  rg_tracer_const("SINK_NONE", RB_RG_TRACER_SINK_NONE);
  rg_tracer_const("SINK_UDP", RB_RG_TRACER_SINK_UDP);
  rg_tracer_const("SINK_TCP", RB_RG_TRACER_SINK_TCP);
  rg_tracer_const("SINK_CALLBACK", RB_RG_TRACER_SINK_CALLBACK);

#ifdef RB_RG_DEBUG
  rb_define_const(rb_cRaygunTracer, "DEBUG_BUILD", Qtrue);
#else
  // To determine if we're running a DEBUG build or not
  rb_define_const(rb_cRaygunTracer, "DEBUG_BUILD", Qfalse);
#endif

  // Conditional compile time feature predicates
#ifdef RB_RG_EMIT_ARGUMENTS
  rb_define_const(rb_cRaygunTracer, "FEATURE_EMIT_ARGUMENTS", Qtrue);
#else
  rb_define_const(rb_cRaygunTracer, "FEATURE_EMIT_ARGUMENTS", Qfalse);
#endif

#ifdef RB_RG_TRACE_BLOCKS
  rb_define_const(rb_cRaygunTracer, "FEATURE_TRACE_BLOCKS", Qtrue);
#else
  rb_define_const(rb_cRaygunTracer, "FEATURE_TRACE_BLOCKS", Qfalse);
#endif

  // Hook up the custom allocator the Raygun::Apm::Tracer class
  rb_define_alloc_func(rb_cRaygunTracer, rb_rg_tracer_alloc);

  // Defines the method table for the tracer
  rb_define_method(rb_cRaygunTracer, "disable_tracepoints", rb_rg_tracer_disable_tracepoints, 0);
  rb_define_method(rb_cRaygunTracer, "log_level=", rb_rg_tracer_log_level_equals, 1);
  rb_define_method(rb_cRaygunTracer, "environment=", rb_rg_tracer_environment_equals, 1);
  rb_define_method(rb_cRaygunTracer, "api_key=", rb_rg_tracer_api_key_equals, 1);
  rb_define_method(rb_cRaygunTracer, "debug_blacklist=", rb_rg_tracer_debug_blacklist_equals, 1);
  rb_define_method(rb_cRaygunTracer, "process_ended", rb_rg_tracer_process_ended, 0);
  rb_define_method(rb_cRaygunTracer, "start_trace", rb_rg_tracer_start_trace, 0);
  rb_define_method(rb_cRaygunTracer, "end_trace", rb_rg_tracer_end_trace, 0);
  rb_define_method(rb_cRaygunTracer, "callback_sink=", rb_rg_tracer_callback_sink_set, 1);
  rb_define_method(rb_cRaygunTracer, "emit", rb_rg_tracer_emit, 1);
  rb_define_method(rb_cRaygunTracer, "get_thread_id", rb_rg_get_thread_id, 1);

  // XXX extract #whitelist and #blacklist and use enumerable on them?
  rb_define_method(rb_cRaygunTracer, "add_blacklist", rb_rg_tracer_add_blacklist, 2);
  rb_define_method(rb_cRaygunTracer, "add_whitelist", rb_rg_tracer_add_whitelist, 2);
  rb_define_method(rb_cRaygunTracer, "show_filters", rb_rg_tracer_show_filters, 0);
  rb_define_method(rb_cRaygunTracer, "register_libraries", rb_rg_tracer_register_libraries, 1);
  rb_define_method(rb_cRaygunTracer, "blacklisted?", rb_rg_tracer_blacklisted_p, 3);
  rb_define_method(rb_cRaygunTracer, "whitelisted?", rb_rg_tracer_whitelisted_p, 3);

  rb_define_method(rb_cRaygunTracer, "udp_sink", rb_rg_tracer_udp_sink_set, -1);
  rb_define_method(rb_cRaygunTracer, "tcp_sink", rb_rg_tracer_tcp_sink_set, -1);
  rb_define_method(rb_cRaygunTracer, "now", rb_rg_tracer_now, 0);
  rb_define_method(rb_cRaygunTracer, "noop!", rb_rg_tracer_noop_bang, 0);

  // For testing
  rb_define_method(rb_cRaygunTracer, "memory_address", rb_rg_tracer_memory_address, 1);

  // Debugging specific - requires PROTON_DIAGNOSTICS to be set
  rb_define_method(rb_cRaygunTracer, "diagnostics", rb_rg_tracer_diagnostics, 0);
}
