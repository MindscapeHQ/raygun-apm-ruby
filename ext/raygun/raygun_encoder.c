#include "raygun.h"

// Default blackhole sink - a noop effectively
static int rg_event_sink_blackhole(rg_context_t *context, void *userdata, const rg_event_t *event, const rg_length_t size)
{
  return 1;
}

// Allocator for the main Raygun specific context which contains:
// * The pid - once off lookup on init, stable for the rest of the profiler runtime
// * Assigns a default blackhole sink
// * Assigns the timestamper to use
//
// The scratch buffer for encoding is a static buffer on the struct and thus no need to allocate explicitly
rg_context_t *rg_context_alloc()
{
  rg_context_t *context = calloc(1, sizeof(rg_context_t));
  if (!context) return NULL;

  // once off pid lookup for the duration of the process
  context->pid = rg_getpid();
  // Puggable event sink - defaults to blackhole
  context->sink = rg_event_sink_blackhole;
  // Pluggable timestamp generator (to faciliate testing) - see raygun_platform.c
  context->timestamper = rg_timestamp;
  return context;
}

// Populates the header part of a new wire protocol command being encoded
static inline void rg_fill_header(const rg_context_t *context, rg_event_t *event, const rg_short_t size)
{
  event->length = size;
  event->pid = context->pid;
  event->timestamp = context->timestamper();
}

// Actually encodes the header for a new wire protocol command
rg_short_t rg_encode_header(rg_context_t *context, rg_event_t *event, rg_byte_t *ptr, const rg_short_t size)
{
  rg_fill_header(context, event, size);
  return rg_encode_header_impl(ptr, event);
}

// Implementation of the header encoder
rg_short_t rg_encode_header_impl(rg_byte_t *ptr, rg_event_t *event)
{
#ifdef RG_DEBUG
  rg_byte_t *offset = ptr;
  rg_short_t size;
#endif
  memcpy(ptr, &event->length, sizeof(event->length)); ptr+= sizeof(event->length);
  memcpy(ptr, &event->type, sizeof(event->type)); ptr+= sizeof(event->type);
  memcpy(ptr, &event->pid, sizeof(event->pid)); ptr+= sizeof(event->pid);
  memcpy(ptr, &event->tid, sizeof(event->tid)); ptr+= sizeof(event->tid);
  memcpy(ptr, &event->timestamp, sizeof(event->timestamp)); ptr+= sizeof(event->timestamp);
#ifdef RG_DEBUG
  size = (rg_short_t)(ptr - offset);
  assert(size == RG_MIN_PAYLOAD);
#endif
  return RG_MIN_PAYLOAD;
}

// Calculates the size of CT_METHODINFO
rg_short_t rg_encode_methodinfo_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.methodinfo.function_id) +
    sizeof(event->data.methodinfo.class_name.length) +
    event->data.methodinfo.class_name.length +
    sizeof(event->data.methodinfo.method_name.length) +
    event->data.methodinfo.method_name.length +
    sizeof(event->data.methodinfo.method_source);
}

// Encodes for CT_METHODINFO as per spec
rg_short_t rg_encode_methodinfo(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.methodinfo.function_id, sizeof(event->data.methodinfo.function_id)); ptr+= sizeof(event->data.methodinfo.function_id);
  memcpy(ptr, &event->data.methodinfo.class_name.length, sizeof(event->data.methodinfo.class_name.length)); ptr+= sizeof(event->data.methodinfo.class_name.length);
  memcpy(ptr, event->data.methodinfo.class_name.string, event->data.methodinfo.class_name.length); ptr+= event->data.methodinfo.class_name.length;
  memcpy(ptr, &event->data.methodinfo.method_name.length, sizeof(event->data.methodinfo.method_name.length)); ptr+= sizeof(event->data.methodinfo.method_name.length);
  memcpy(ptr, event->data.methodinfo.method_name.string, event->data.methodinfo.method_name.length); ptr+= event->data.methodinfo.method_name.length;
  memcpy(ptr, &event->data.methodinfo.method_source, sizeof(event->data.methodinfo.method_source)); ptr+= sizeof(event->data.methodinfo.method_source);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_methodinfo_size(event));
#endif
  return size;
}

// Calculates the size of CT_SQL_INFORMATION
rg_short_t rg_encode_sql_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.sql.provider.encoding) +
    sizeof(event->data.sql.provider.length) +
    event->data.sql.provider.length +
    sizeof(event->data.sql.host.encoding) +
    sizeof(event->data.sql.host.length) +
    event->data.sql.host.length +
    sizeof(event->data.sql.database.encoding) +
    sizeof(event->data.sql.database.length) +
    event->data.sql.database.length +
    sizeof(event->data.sql.query.encoding) +
    sizeof(event->data.sql.query.length) +
    event->data.sql.query.length +
    sizeof(event->data.sql.duration);
}

// Encodes CT_SQL_INFORMATION as per spec
rg_short_t rg_encode_sql(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.sql.provider.encoding, sizeof(event->data.sql.provider.encoding)); ptr+= sizeof(event->data.sql.provider.encoding);
  memcpy(ptr, &event->data.sql.provider.length, sizeof(event->data.sql.provider.length)); ptr+= sizeof(event->data.sql.provider.length);
  memcpy(ptr, event->data.sql.provider.string, event->data.sql.provider.length); ptr+= event->data.sql.provider.length;

  memcpy(ptr, &event->data.sql.host.encoding, sizeof(event->data.sql.host.encoding)); ptr+= sizeof(event->data.sql.host.encoding);
  memcpy(ptr, &event->data.sql.host.length, sizeof(event->data.sql.host.length)); ptr+= sizeof(event->data.sql.host.length);
  memcpy(ptr, event->data.sql.host.string, event->data.sql.host.length); ptr+= event->data.sql.host.length;

  memcpy(ptr, &event->data.sql.database.encoding, sizeof(event->data.sql.database.encoding)); ptr+= sizeof(event->data.sql.database.encoding);
  memcpy(ptr, &event->data.sql.database.length, sizeof(event->data.sql.database.length)); ptr+= sizeof(event->data.sql.database.length);
  memcpy(ptr, event->data.sql.database.string, event->data.sql.database.length); ptr+= event->data.sql.database.length;

  memcpy(ptr, &event->data.sql.query.encoding, sizeof(event->data.sql.query.encoding)); ptr+= sizeof(event->data.sql.query.encoding);
  memcpy(ptr, &event->data.sql.query.length, sizeof(event->data.sql.query.length)); ptr+= sizeof(event->data.sql.query.length);
  memcpy(ptr, event->data.sql.query.string, event->data.sql.query.length); ptr+= event->data.sql.query.length;

  memcpy(ptr, &event->data.sql.duration, sizeof(event->data.sql.duration)); ptr+= sizeof(event->data.sql.duration);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_sql_size(event));
#endif
  return size;
}

// Calculates the size of CT_HTTP_INCOMING_INFORMATION
rg_short_t rg_encode_http_in_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.http_in.url.length) +
    event->data.http_in.url.length +
    sizeof(event->data.http_in.verb.length) +
    event->data.http_in.verb.length +
    sizeof(event->data.http_in.status) +
    sizeof(event->data.http_in.duration);
}

// Encodes CT_HTTP_INCOMING_INFORMATION as per spec
rg_short_t rg_encode_http_in(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.http_in.url.length, sizeof(event->data.http_in.url.length)); ptr+= sizeof(event->data.http_in.url.length);
  memcpy(ptr, event->data.http_in.url.string, event->data.http_in.url.length); ptr+= event->data.http_in.url.length;
  memcpy(ptr, &event->data.http_in.verb.length, sizeof(event->data.http_in.verb.length)); ptr+= sizeof(event->data.http_in.verb.length);
  memcpy(ptr, event->data.http_in.verb.string, event->data.http_in.verb.length); ptr+= event->data.http_in.verb.length;
  memcpy(ptr, &event->data.http_in.status, sizeof(event->data.http_in.status)); ptr+= sizeof(event->data.http_in.status);
  memcpy(ptr, &event->data.http_in.duration, sizeof(event->data.http_in.duration)); ptr+= sizeof(event->data.http_in.duration);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_http_in_size(event));
#endif
  return size;
}

// Calculates the size of CT_PROCESS_TYPE
rg_short_t rg_encode_process_type_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.process_type.technology_type.length) +
    event->data.process_type.technology_type.length +
    sizeof(event->data.process_type.process_type.length) +
    event->data.process_type.process_type.length;
}

// Encodes CT_PROCESS_TYPE as per spec
rg_short_t rg_encode_process_type(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.process_type.technology_type.length, sizeof(event->data.process_type.technology_type.length)); ptr+= sizeof(event->data.process_type.technology_type.length);
  memcpy(ptr, &event->data.process_type.technology_type.string, event->data.process_type.technology_type.length); ptr+= event->data.process_type.technology_type.length;
  memcpy(ptr, &event->data.process_type.process_type.length, sizeof(event->data.process_type.process_type.length)); ptr+= sizeof(event->data.process_type.process_type.length);
  memcpy(ptr, &event->data.process_type.process_type.string, event->data.process_type.process_type.length); ptr+= event->data.process_type.process_type.length;
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_process_type_size(event));
#endif
  return size;
}

// Calculates the size of CT_BEGIN_TRANSACTION
rg_short_t rg_encode_begin_transaction_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.begin_transaction.api_key.length) +
    event->data.begin_transaction.api_key.length +
    sizeof(event->data.begin_transaction.technology_type.length) +
    event->data.begin_transaction.technology_type.length +
    sizeof(event->data.begin_transaction.process_type.length) +
    event->data.begin_transaction.process_type.length;
}

// Encodes CT_BEGIN_TRANSACTION as per spec
rg_short_t rg_encode_begin_transaction(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.begin_transaction.api_key.length, sizeof(event->data.begin_transaction.api_key.length)); ptr+= sizeof(event->data.begin_transaction.api_key.length);
  memcpy(ptr, &event->data.begin_transaction.api_key.string, event->data.begin_transaction.api_key.length); ptr+= event->data.begin_transaction.api_key.length;
  memcpy(ptr, &event->data.begin_transaction.technology_type.length, sizeof(event->data.begin_transaction.technology_type.length)); ptr+= sizeof(event->data.begin_transaction.technology_type.length);
  memcpy(ptr, &event->data.begin_transaction.technology_type.string, event->data.begin_transaction.technology_type.length); ptr+= event->data.begin_transaction.technology_type.length;
  memcpy(ptr, &event->data.begin_transaction.process_type.length, sizeof(event->data.begin_transaction.process_type.length)); ptr+= sizeof(event->data.begin_transaction.process_type.length);
  memcpy(ptr, &event->data.begin_transaction.process_type.string, event->data.begin_transaction.process_type.length); ptr+= event->data.begin_transaction.process_type.length;
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_begin_transaction_size(event));
#endif
  return size;
}

// Calculates the size of CT_HTTP_OUTGOING_INFORMATION
rg_short_t rg_encode_http_out_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.http_out.url.length) +
    event->data.http_out.url.length +
    sizeof(event->data.http_out.verb.length) +
    event->data.http_out.verb.length +
    sizeof(event->data.http_out.status) +
    sizeof(event->data.http_out.duration);
}

// Encodes CT_HTTP_OUTGOING_INFORMATION as per spec
rg_short_t rg_encode_http_out(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.http_out.url.length, sizeof(event->data.http_out.url.length)); ptr+= sizeof(event->data.http_out.url.length);
  memcpy(ptr, event->data.http_out.url.string, event->data.http_out.url.length); ptr+= event->data.http_out.url.length;
  memcpy(ptr, &event->data.http_out.verb.length, sizeof(event->data.http_out.verb.length)); ptr+= sizeof(event->data.http_out.verb.length);
  memcpy(ptr, event->data.http_out.verb.string, event->data.http_out.verb.length); ptr+= event->data.http_out.verb.length;
  memcpy(ptr, &event->data.http_out.status, sizeof(event->data.http_out.status)); ptr+= sizeof(event->data.http_out.status);
  memcpy(ptr, &event->data.http_out.duration, sizeof(event->data.http_out.duration)); ptr+= sizeof(event->data.http_out.duration);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_http_out_size(event));
#endif
  return size;
}

// Calculates the size of a supported variable type (very rarely used as we don't emit arguments by default)
rg_short_t rg_encode_variableinfo_size(const rg_variable_info_t *variableinfo)
{
  rg_short_t size = sizeof(variableinfo->length) +
    sizeof(variableinfo->type) +
    sizeof(variableinfo->name_length) +
    variableinfo->name_length;
  switch (variableinfo->type) {
    case RG_VT_BOOLEAN:
         size += sizeof(variableinfo->as.t_boolean);
         break;
    case RG_VT_STRING:
         size += sizeof(variableinfo->as.t_encoded_string.length) + variableinfo->as.t_encoded_string.length;
         break;
    case RG_VT_LARGESTRING:
         size += sizeof(variableinfo->as.t_largestring.length) + variableinfo->as.t_largestring.length;
         break;
    case RG_VT_FLOAT:
         size += sizeof(variableinfo->as.t_float);
         break;
    case RG_VT_SHORT:
         size += sizeof(variableinfo->as.t_short);
         break;
    case RG_VT_UNSIGNED_SHORT:
         size += sizeof(variableinfo->as.t_unsigned_short);
         break;
    case RG_VT_INT32:
         size += sizeof(variableinfo->as.t_int32);
         break;
    case RG_VT_UNSIGNED_INT32:
         size += sizeof(variableinfo->as.t_unsigned_int32);
         break;
    case RG_VT_LONG:
         size += sizeof(variableinfo->as.t_long);
         break;
    case RG_VT_UNSIGNED_LONG:
         size += sizeof(variableinfo->as.t_unsigned_long);
         break;
    case RG_VT_VOID:
         size += 0;
         break;
  }
  return size;
}

// Encodes the variable type as per spec (very rarely used as we don't emit arguments by default)
rg_short_t rg_encode_variableinfo(rg_byte_t *ptr, rg_variable_info_t *variableinfo)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &variableinfo->length, sizeof(variableinfo->length)); ptr+= sizeof(variableinfo->length);
  memcpy(ptr, &variableinfo->type, sizeof(variableinfo->type)); ptr+= sizeof(variableinfo->type);
  memcpy(ptr, &variableinfo->name_length, sizeof(variableinfo->name_length)); ptr+= sizeof(variableinfo->name_length);
  memcpy(ptr, variableinfo->name, variableinfo->name_length); ptr+= variableinfo->name_length;
  switch (variableinfo->type) {
    case RG_VT_BOOLEAN:
         memcpy(ptr, &variableinfo->as.t_boolean, sizeof(variableinfo->as.t_boolean)); ptr+= sizeof(variableinfo->as.t_boolean);
         break;
    case RG_VT_STRING:
         memcpy(ptr, &variableinfo->as.t_encoded_string.length, sizeof(variableinfo->as.t_encoded_string.length)); ptr+= sizeof(variableinfo->as.t_encoded_string.length);
         memcpy(ptr, variableinfo->as.t_encoded_string.string, variableinfo->as.t_encoded_string.length); ptr+= variableinfo->as.t_encoded_string.length;
         break;
    case RG_VT_LARGESTRING:
         memcpy(ptr, &variableinfo->as.t_largestring.length, sizeof(variableinfo->as.t_largestring.length)); ptr+= sizeof(variableinfo->as.t_largestring.length);
         memcpy(ptr, variableinfo->as.t_largestring.string, variableinfo->as.t_largestring.length); ptr+= variableinfo->as.t_largestring.length;
         break;
    case RG_VT_FLOAT:
         memcpy(ptr, &variableinfo->as.t_float, sizeof(variableinfo->as.t_float)); ptr+= sizeof(variableinfo->as.t_float);
         break;
    case RG_VT_SHORT:
         memcpy(ptr, &variableinfo->as.t_short, sizeof(variableinfo->as.t_short)); ptr+= sizeof(variableinfo->as.t_short);
         break;
    case RG_VT_UNSIGNED_SHORT:
         memcpy(ptr, &variableinfo->as.t_unsigned_short, sizeof(variableinfo->as.t_unsigned_short)); ptr+= sizeof(variableinfo->as.t_unsigned_short);
         break;
    case RG_VT_INT32:
         memcpy(ptr, &variableinfo->as.t_int32, sizeof(variableinfo->as.t_int32)); ptr+= sizeof(variableinfo->as.t_int32);
         break;
    case RG_VT_UNSIGNED_INT32:
         memcpy(ptr, &variableinfo->as.t_unsigned_int32, sizeof(variableinfo->as.t_unsigned_int32)); ptr+= sizeof(variableinfo->as.t_unsigned_int32);
         break;
    case RG_VT_LONG:
         memcpy(ptr, &variableinfo->as.t_long, sizeof(variableinfo->as.t_long)); ptr+= sizeof(variableinfo->as.t_long);
         break;
    case RG_VT_UNSIGNED_LONG:
         memcpy(ptr, &variableinfo->as.t_unsigned_long, sizeof(variableinfo->as.t_unsigned_long)); ptr+= sizeof(variableinfo->as.t_unsigned_long);
         break;
  }
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size == rg_encode_variableinfo_size(variableinfo));
#endif
  return size;
}

// Calculates the size of CT_BEGIN
rg_short_t rg_encode_begin_size(const rg_event_t *event)
{
  rg_short_t size = RG_MIN_PAYLOAD +
    sizeof(event->data.begin.function_id) +
    sizeof(event->data.begin.instance_id) +
    sizeof(event->data.begin.argc);
#ifdef RB_RG_EMIT_ARGUMENTS
  for (int i = 0; i < event->data.begin.argc; i++) {
    rg_variable_info_t varinfo = event->data.begin.args[i];
    size+= rg_encode_variableinfo_size(&varinfo);
  }
#endif
  return size;
}

// Encodes CT_BEGIN as per spec
rg_short_t rg_encode_begin(rg_byte_t *ptr, rg_event_t *event)
{
  rg_short_t size;
  rg_byte_t *offset = ptr;
  memcpy(ptr, &event->data.begin.function_id, sizeof(event->data.begin.function_id)); ptr+= sizeof(event->data.begin.function_id);
  memcpy(ptr, &event->data.begin.instance_id, sizeof(event->data.begin.instance_id)); ptr+= sizeof(event->data.begin.instance_id);
  memcpy(ptr, &event->data.begin.argc, sizeof(event->data.begin.argc)); ptr+= sizeof(event->data.begin.argc);
#ifdef RB_RG_EMIT_ARGUMENTS
  for (int i = 0; i < event->data.begin.argc; i++) {
    rg_variable_info_t varinfo = event->data.begin.args[i];
    ptr+= rg_encode_variableinfo(ptr, &varinfo);
  }
#endif
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_begin_size(event));
#endif
  return size;
}

// Calculates the size of CT_END
rg_short_t rg_encode_end_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.end.function_id) +
    sizeof(event->data.end.tail_call) +
    rg_encode_variableinfo_size((rg_variable_info_t*)(&event->data.end.returnvalue));
}

// Encodes CT_END as per spec
rg_short_t rg_encode_end(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.end.function_id, sizeof(event->data.end.function_id)); ptr+= sizeof(event->data.end.function_id);
  memcpy(ptr, &event->data.end.tail_call, sizeof(event->data.end.tail_call)); ptr+= sizeof(event->data.end.tail_call);
  ptr+= rg_encode_variableinfo(ptr, (rg_variable_info_t*)(&event->data.end.returnvalue));
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_end_size(event));
#endif
  return size;
}

// Helper to encode the profiler buffer and length into a batch's static buffer and increment the batch counter
// Range checks are caller responsibility for overflow etc.
//
void rg_encode_into_batch(const rg_byte_t *buf, const rg_length_t buflen, rg_event_batch_t *batch)
{
  memcpy((batch->buf + batch->length), buf, buflen);
  batch->length += buflen;
  batch->count += 1;
}

// Encodes CT_BATCH as per spec
void rg_encode_batch_header(rg_event_batch_t *batch)
{
  rg_byte_t *ptr = batch->buf;
  memcpy(ptr, &batch->length, sizeof(batch->length)); ptr+= sizeof(batch->length);
  memcpy(ptr, &batch->type, sizeof(batch->type)); ptr+= sizeof(batch->type);
  memcpy(ptr, &batch->count, sizeof(batch->count)); ptr+= sizeof(batch->count);
  memcpy(ptr, &batch->sequence, sizeof(batch->sequence)); ptr+= sizeof(batch->sequence);
  memcpy(ptr, &batch->pid, sizeof(batch->pid));
  batch->sequence +=1;
}

// Calculates the size of CT_PROCESS_FREQUENCY
rg_short_t rg_encode_process_frequency_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.process_frequency.frequency);
}

// Encodes CT_PROCESS_FREQUENCY as per spec
int rg_encode_process_frequency(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.process_frequency.frequency , sizeof(event->data.process_frequency.frequency)); ptr+= sizeof(event->data.process_frequency.frequency);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_process_frequency_size(event));
#endif
  return size;
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_PROCESS_FREQUENCY to the configured sink on context
int rg_process_frequency(rg_context_t *context, void *userdata, rg_tid_t tid, rg_frequency_t frequency)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_PROCESS_FREQUENCY;
  event.tid = tid;
  event.data.process_frequency.frequency = frequency;

  size = rg_encode_process_frequency(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_BEGIN_TRANSACTION to the configured sink on context
int rg_begin_transaction(rg_context_t *context, void *userdata, rg_tid_t tid, rg_encoded_string_t api_key, rg_encoded_string_t technology_type, rg_encoded_string_t process_type)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_BEGIN_TRANSACTION;
  event.tid = tid;
  event.data.begin_transaction.api_key = api_key;
  event.data.begin_transaction.technology_type = technology_type;
  event.data.begin_transaction.process_type = process_type;

  size = rg_encode_begin_transaction(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_END_TRANSACTION to the configured sink on context
int rg_end_transaction(rg_context_t *context, void *userdata, rg_tid_t tid)
{
  rg_event_t event;
  event.type = RG_EVENT_END_TRANSACTION;
  event.tid = tid;

  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_PROCESS_TYPE to the configured sink on context
int rg_process_type(rg_context_t *context, void *userdata, rg_tid_t tid, rg_encoded_string_t technology_type, rg_encoded_string_t process_type)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_PROCESS_TYPE;
  event.tid = tid;
  event.data.process_type.technology_type = technology_type;
  event.data.process_type.process_type = process_type;

  size = rg_encode_process_type(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_PROCESS_ENDED to the configured sink on context
int rg_process_ended(rg_context_t *context, void *userdata, rg_tid_t tid)
{
  rg_event_t event;
  event.type = RG_EVENT_PROCESS_ENDED;
  event.tid = tid;
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD);
}

// Calculates the size of CT_THREAD_START
rg_short_t rg_encode_thread_started_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.thread_started.parent_tid);
}

// Encodes CT_THREAD_START as per spec
rg_short_t rg_encode_thread_started(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.thread_started.parent_tid, sizeof(event->data.thread_started.parent_tid)); ptr+= sizeof(event->data.thread_started.parent_tid);
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_thread_started_size(event));
#endif
  return size;
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_THREAD_START to the configured sink on context
int rg_thread_started(rg_context_t *context, void *userdata, rg_thread_t *th)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_THREAD_STARTED_2;
  event.tid = th->tid;
  event.data.thread_started.parent_tid = th->parent_tid;

  size = rg_encode_thread_started(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD + size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_THREAD_END to the configured sink on context
int rg_thread_ended(rg_context_t *context, void *userdata, rg_tid_t tid)
{
  rg_event_t event;
  event.type = RG_EVENT_THREAD_ENDED;
  event.tid = tid;
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD);
}

// Calculates the size of CT_EXCEPTION_THROWN
rg_short_t rg_encode_exception_thrown_size(const rg_event_t *event)
{
  return RG_MIN_PAYLOAD +
    sizeof(event->data.exception_thrown.exception_id) +
    sizeof(event->data.exception_thrown.class_name.length) +
    event->data.exception_thrown.class_name.length +
    sizeof(event->data.exception_thrown.correlation_id.length) +
    event->data.exception_thrown.correlation_id.length;
}

// Encodes CT_EXCEPTION_THROWN as per spec
rg_short_t rg_encode_exception_thrown(rg_byte_t *ptr, rg_event_t *event)
{
  rg_byte_t *offset = ptr;
  rg_short_t size;
  memcpy(ptr, &event->data.exception_thrown.exception_id , sizeof(event->data.exception_thrown.exception_id)); ptr+= sizeof(event->data.exception_thrown.exception_id);
  memcpy(ptr, &event->data.exception_thrown.class_name.length, sizeof(event->data.exception_thrown.class_name.length)); ptr+= sizeof(event->data.exception_thrown.class_name.length);
  memcpy(ptr, event->data.exception_thrown.class_name.string, event->data.exception_thrown.class_name.length); ptr+= event->data.exception_thrown.class_name.length;
  memcpy(ptr, &event->data.exception_thrown.correlation_id.length, sizeof(event->data.exception_thrown.correlation_id.length)); ptr+= sizeof(event->data.exception_thrown.correlation_id.length);
  memcpy(ptr, event->data.exception_thrown.correlation_id.string, event->data.exception_thrown.correlation_id.length); ptr+= event->data.exception_thrown.correlation_id.length;
  size = (rg_short_t)(ptr - offset);
#ifdef RG_DEBUG
  assert(size + RG_MIN_PAYLOAD == rg_encode_exception_thrown_size(event));
#endif
  return size;
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_EXCEPTION_THROWN to the configured sink on context
int rg_exception_thrown(rg_context_t *context, void *userdata, rg_tid_t tid, rg_exception_instance_id_t exception, rg_encoded_string_t class_name, rg_encoded_string_t correlation_id)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_EXCEPTION_THROWN_2;
  event.tid = tid;
  event.data.exception_thrown.exception_id = exception;
  event.data.exception_thrown.class_name = class_name;
  event.data.exception_thrown.correlation_id = correlation_id;

  size = rg_encode_exception_thrown(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_METHODINFO to the configured sink on context
int rg_methodinfo(rg_context_t *context, void *userdata, rg_tid_t tid, rg_method_t *method, rg_encoded_string_t class_name, rg_encoded_string_t method_name)
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_METHODINFO_2;
  event.tid = tid;
  event.data.methodinfo.function_id = method->function_id;
  event.data.methodinfo.class_name = class_name;
  event.data.methodinfo.method_name = method_name;
  event.data.methodinfo.method_source = (rg_method_source_t)method->source;
  size = rg_encode_methodinfo(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  // Save a copy of the encoded event so we can emit it at intervals
  // back to the agent by stubbing out the tid value of the timer thread
  method->encoded_size = RG_MIN_PAYLOAD+size;
  method->encoded = malloc(method->encoded_size);
  memcpy(method->encoded, context->buf, method->encoded_size);

  return context->sink(context, userdata, &event, method->encoded_size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_BEGIN to the configured sink on context
#ifdef RB_RG_EMIT_ARGUMENTS
int rg_begin(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_instance_id_t instance, rg_argc_t argc, rg_variable_info_t args[])
#else
int rg_begin(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_instance_id_t instance)
#endif
{
  rg_event_t event;
  rg_length_t size;
  event.type = RG_EVENT_BEGIN;
  event.tid = tid;
  event.data.begin.function_id = func;
  event.data.begin.instance_id = instance;
#ifdef RB_RG_EMIT_ARGUMENTS
  event.data.begin.argc = argc;
  for (int i = 0; i < event.data.begin.argc; i++) {
    memcpy(event.data.begin.args, args, sizeof(rg_variable_info_t) * event.data.begin.argc);
  }
#else
  event.data.begin.argc = 0;
#endif
  size = rg_encode_begin(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function called from Ruby (but any generic implementation really) to encode and emit CT_END to the configured sink on context
#ifdef RB_RG_EMIT_ARGUMENTS
int rg_end(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_variable_info_t *returnvalue)
#else
int rg_end(rg_context_t *context, void *userdata, rg_tid_t tid, rg_function_id_t func, rg_void_return_t *returnvalue)
#endif
{
  rg_event_t event;
  rg_length_t size;

  event.type = RG_EVENT_END;
  event.tid = tid;
  event.data.end.function_id = func;
  // TODO: support tail call
  event.data.end.tail_call = 0;
  // TODO: implicit return should NOT encode the implicit nil return val
  event.data.end.returnvalue = *returnvalue;
  size = rg_encode_end(context->buf + RG_MIN_PAYLOAD, &event);
  rg_encode_header(context, &event, context->buf, RG_MIN_PAYLOAD + size);

  return context->sink(context, userdata, &event, RG_MIN_PAYLOAD+size);
}

// Helper function for event coercion - see raygun_event.c
rg_short_t rg_encode_size(const rg_event_t *event)
{
  switch ((rg_event_type_t)event->type) {
  case RG_EVENT_BEGIN:
    return rg_encode_begin_size(event);
  case RG_EVENT_END:
    return rg_encode_end_size(event);
  case RG_EVENT_METHODINFO_2:
    return rg_encode_methodinfo_size(event);
  case RG_EVENT_SQL_INFORMATION:
    return rg_encode_sql_size(event);
  case RG_EVENT_HTTP_INCOMING_INFORMATION:
    return rg_encode_http_in_size(event);
  case RG_EVENT_HTTP_OUTGOING_INFORMATION:
    return rg_encode_http_out_size(event);
  case RG_EVENT_BATCH:
    // XXX not defined, picked up as failure in X compile flows
    return 0;//rg_encode_batch_size(event);
  case RG_EVENT_PROCESS_FREQUENCY:
    return rg_encode_process_frequency_size(event);
  case RG_EVENT_EXCEPTION_THROWN_2:
    return rg_encode_exception_thrown_size(event);
  case RG_EVENT_PROCESS_TYPE:
    return rg_encode_process_type_size(event);
  case RG_EVENT_BEGIN_TRANSACTION:
    return rg_encode_begin_transaction_size(event);
  case RG_EVENT_THREAD_STARTED_2:
    return rg_encode_thread_started_size(event);
  case RG_EVENT_THREAD_ENDED:
  case RG_EVENT_PROCESS_ENDED:
  case RG_EVENT_END_TRANSACTION:
    goto min_size;
  }

min_size:
  return RG_MIN_PAYLOAD;
}
