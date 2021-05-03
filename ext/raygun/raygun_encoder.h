#ifndef RAYGUN_ENCODER_H
#define RAYGUN_ENCODER_H

#include "raygun_protocol.h"
#include "raygun_platform.h"

struct rg_context_t;

// Main container for tracking encoder state. The buffer is just scratch space before emitting
// a full event to a sink. Support for event sinks is implemented through a callback function.
// Ditto for timestamping so we can stub that out in unit tests for asserting wire protocol blobs.
typedef struct rg_context {
  // Static for the duration of the process, infer once
  rg_unsigned_int_t pid;
  // Timestamper (for swapping out in tests)
  rg_timestamp_t(*timestamper)();
  // Observed event sink - all the encoder helper functions call into this
  int(*sink)(struct rg_context *context, void *userdata, const rg_event_t *event, const rg_length_t size);
  // Scratch buffer for pluggable transport
  rg_byte_t buf[RG_ENCODER_SCRATCH_BUFFER_SIZE];
} rg_context_t;

// Core encoder API

rg_short_t rg_encode_header(rg_context_t *context, rg_event_t *event, rg_byte_t *ptr, const rg_length_t size);
rg_short_t rg_encode_header_impl(rg_byte_t *ptr, rg_event_t *event);

rg_short_t rg_encode_thread_started(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_exception_thrown(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_methodinfo(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_variableinfo(rg_byte_t *ptr, rg_variable_info_t *variableinfo);
rg_short_t rg_encode_begin(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_end(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_sql(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_http_in(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_http_out(rg_byte_t *ptr, rg_event_t *event);
void rg_encode_batch_header(rg_event_batch_t *batch);
void rg_encode_into_batch(const rg_byte_t *buf, const rg_length_t buflen, rg_event_batch_t *batch);
rg_short_t rg_encode_begin_transaction(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_process_type(rg_byte_t *ptr, rg_event_t *event);
rg_short_t rg_encode_size(const rg_event_t *event);

// Context init

rg_context_t *rg_context_alloc();

// Event handler APIs - encodes to raw wire protocol and invokes the sink callback function

int rg_process_frequency(rg_context_t *context, void *userdata, rg_tid_t tid, rg_frequency_t frequency);
int rg_process_type(rg_context_t *context, void *userdata, rg_tid_t tid, rg_encoded_string_t technology_type, rg_encoded_string_t process_type);
int rg_process_ended(rg_context_t *context, void *userdata, rg_tid_t tid);

int rg_thread_started(rg_context_t *context, void *userdata, rg_thread_t *th);
int rg_thread_ended(rg_context_t *context, void *userdata, rg_tid_t tid);

int rg_exception_thrown(rg_context_t *context, void *userdata, rg_tid_t tid, rg_exception_instance_id_t exception, rg_encoded_string_t class_name, rg_encoded_string_t correlation_id);
int rg_methodinfo(rg_context_t *context, void *userdata, rg_tid_t tid, rg_method_t *method, rg_encoded_string_t class_name, rg_encoded_string_t method_name);
#ifdef RB_RG_EMIT_ARGUMENTS
int rg_begin(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_instance_id_t instance, rg_argc_t argc, rg_variable_info_t args[]);
int rg_end(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_variable_info_t *returnvalue);
#else
int rg_begin(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_instance_id_t instance);
int rg_end(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_void_return_t *returnvalue);
#endif

int rg_begin_transaction(rg_context_t *context, void *userdata, rg_tid_t tid, rg_encoded_string_t api_key, rg_encoded_string_t technology_type, rg_encoded_string_t process_type);
int rg_end_transaction(rg_context_t *context, void *userdata, rg_tid_t tid);

#endif
