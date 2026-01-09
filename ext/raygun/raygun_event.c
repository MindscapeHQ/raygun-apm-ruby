#include "raygun_event.h"

VALUE rb_cRaygunEvent,
  rb_cRaygunEventBegin,
  rb_cRaygunEventEnd,
  rb_cRaygunEventMethodinfo,
  rb_cRaygunEventExceptionThrown,
  rb_cRaygunEventThreadStarted,
  rb_cRaygunEventThreadEnded,
  rb_cRaygunEventProcessStarted,
  rb_cRaygunEventProcessType,
  rb_cRaygunEventProcessEnded,
  rb_cRaygunEventProcessFrequency,
  rb_cRaygunEventBatch,
  rb_cRaygunEventSql,
  rb_cRaygunEventHttpIn,
  rb_cRaygunEventHttpOut,
  rb_cRaygunEventBeginTransaction,
  rb_cRaygunEventEndTransaction;

static ID rb_rg_id_escape,
  rb_rg_id_pid,
  rb_rg_id_tid,
  rb_rg_id_timestamp,
  rb_rg_id_exception_id,
  rb_rg_id_function_id,
  rb_rg_id_instance_id,
  rb_rg_id_tailcall,
  rb_rg_id_technology_type,
  rb_rg_id_process_type,
  rb_rg_id_class_name,
  rb_rg_id_method_name,
  rb_rg_id_frequency,
  rb_rg_id_provider,
  rb_rg_id_host,
  rb_rg_id_database,
  rb_rg_id_query,
  rb_rg_id_url,
  rb_rg_id_verb,
  rb_rg_id_status,
  rb_rg_id_method_source,
  rb_rg_id_duration,
  rb_rg_id_correlation_id,
  rb_rg_id_api_key,
  rb_rg_id_parent_tid;

// The main typed data struct that helps to inform the VM (mostly the GC) on how to handle a wrapped structure
// References https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-
//
// We only wrap the raw protocol events and these structs know NOTHING about Ruby and as such the mark callback is
// empty as there's nothing to let the Ruby GC know about with regards to object reachability.
const rb_data_type_t rb_rg_event_type = {
  .wrap_struct_name = "rb_rg_event",
  .function = {
	.dmark = NULL,
	.dfree = rb_rg_event_free,
	.dsize = rb_rg_event_size,
  },
  .data = NULL,
  .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

// The main GC callback from the typed data (https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-) struct.
// Typically it's the responsibility of this method to walk all struct members with data to free so that at the end of this function, if we free the events
// struct, there's nothing dangling about on the heap.
//
void rb_rg_event_free(void *ptr)
{
  rg_event_t *event = (rg_event_t *)ptr;
  if (!event) return;
  xfree(event);
  event = NULL;
}

// Used by ObjectSpace to estimate the size of a Ruby object. This needs to account for all the retained memory of the object and requires walking any
// collection specific struct members and anything else malloc heap allocated.
//
size_t rb_rg_event_size(const void *event)
{
  return sizeof(rg_event_t);
}

// Helper for mapping Events created in Ruby land to their protocol equivalents
static rg_event_type_t rb_rg_event_class2type(VALUE klass)
{
    if(klass == rb_cRaygunEventBegin) return RG_EVENT_BEGIN;
    if(klass == rb_cRaygunEventEnd) return RG_EVENT_END;
    if(klass == rb_cRaygunEventMethodinfo) return RG_EVENT_METHODINFO_2;
    if(klass == rb_cRaygunEventExceptionThrown) return RG_EVENT_EXCEPTION_THROWN_2;
    if(klass == rb_cRaygunEventThreadStarted) return RG_EVENT_THREAD_STARTED_2;
    if(klass == rb_cRaygunEventThreadEnded) return RG_EVENT_THREAD_ENDED;
    if(klass == rb_cRaygunEventProcessType) return RG_EVENT_PROCESS_TYPE;
    if(klass == rb_cRaygunEventProcessEnded) return RG_EVENT_PROCESS_ENDED;
    if(klass == rb_cRaygunEventProcessFrequency) return RG_EVENT_PROCESS_FREQUENCY;
    if(klass == rb_cRaygunEventBatch) return RG_EVENT_BATCH;
    if(klass == rb_cRaygunEventSql) return RG_EVENT_SQL_INFORMATION;
    if(klass == rb_cRaygunEventHttpIn) return RG_EVENT_HTTP_INCOMING_INFORMATION;
    if(klass == rb_cRaygunEventHttpOut) return RG_EVENT_HTTP_OUTGOING_INFORMATION;
    if(klass == rb_cRaygunEventBeginTransaction) return RG_EVENT_BEGIN_TRANSACTION;
    if(klass == rb_cRaygunEventEndTransaction) return RG_EVENT_END_TRANSACTION;
    rb_raise(rb_eRaygunFatal, "Unknown event type: %s", RSTRING_PTR(rb_obj_as_string(klass)));
}

// Allocation helper for allocating a blank event type and let a Ruby land object wrap the allocated struct. Use in production code paths for
// SQL queries and HTTP IN and OUT events, but exercised for all events produced by the callback sink in unit tests.
//
// Nothing fancy here - just a direct wrap of rb_event_t structs to Raygun::Apm::Event* instances
//
static VALUE rb_rg_event_alloc(VALUE klass)
{
  rg_event_t *event = ZALLOC(rg_event_t);
  event->type = rb_rg_event_class2type(klass);
  return TypedData_Wrap_Struct(klass, &rb_rg_event_type, event);
}

// The main setter interface for events. This API had it's origins at the very beginning of the project and was opaque to begin with but a
// better API would be to explicitly define the expected members to be set for a particular Event type and raise an error on encode if all
// member's are not set as there's a risk of segfault for NOT setting a particular required member such as a missing query for a SQL event etc.
//
static VALUE rb_rg_event_aset(VALUE obj, VALUE attr, VALUE val)
{
  rb_rg_get_event(obj);
  if (TYPE(attr) != T_SYMBOL) rb_raise(rb_eRaygunFatal, "Attribute expected to be a symbol:%s!", RSTRING_PTR(rb_obj_as_string(attr)));
  const ID symbol = SYM2ID(attr);

  if (symbol == rb_rg_id_pid)
  {
    event->pid = (rg_pid_t)rb_rg_encode_unsigned_long(val);
  } else if (symbol == rb_rg_id_tid)
  {
    event->tid = (rg_tid_t)rb_rg_encode_unsigned_long(val);
  } else if (symbol == rb_rg_id_parent_tid)
  {
    event->data.thread_started.parent_tid = (rg_tid_t)rb_rg_encode_unsigned_long(val);
  } else if (symbol == rb_rg_id_timestamp)
  {
    event->timestamp = (rg_timestamp_t)NUM2LL(val);
  } else if (symbol == rb_rg_id_frequency)
  {
    event->data.process_frequency.frequency = (rg_frequency_t)NUM2LL(val);
  } else if (symbol == rb_rg_id_exception_id)
  {
    event->data.exception_thrown.exception_id = NUM2ULL(val);
  } else if (symbol == rb_rg_id_function_id)
  {
    event->data.function_id = (rg_function_id_t)NUM2UINT(val);
  } else if (symbol == rb_rg_id_instance_id)
  {
    event->data.begin.instance_id = (rg_instance_id_t)NUM2ULL(val);
  } else if (symbol == rb_rg_id_tailcall)
  {
    event->data.end.tail_call = (rg_boolean_t)RTEST(val);
  } else if (symbol == rb_rg_id_method_source)
  {
    event->data.methodinfo.method_source = (rg_byte_t)NUM2UINT(val);
  } else if (symbol == rb_rg_id_class_name)
  {
    if (event->type == RG_EVENT_METHODINFO_2) {
      event->data.methodinfo.class_name.encoding = RG_STRING_ENCODING_ASCII;
      rb_rg_encode_string(&event->data.methodinfo.class_name, val, Qnil);
    } else if (event->type == RG_EVENT_EXCEPTION_THROWN_2) {
      event->data.exception_thrown.class_name.encoding = RG_STRING_ENCODING_ASCII;
      rb_rg_encode_string(&event->data.exception_thrown.class_name, val, Qnil);
    }
  } else if (symbol == rb_rg_id_method_name)
  {
    // XXX assumed ascii for now until spec updates
    event->data.methodinfo.method_name.encoding = RG_STRING_ENCODING_ASCII;
    rb_rg_encode_string(&event->data.methodinfo.method_name, val, Qnil);
  } else if (symbol == rb_rg_id_provider)
  {
    event->data.sql.provider.encoding = RG_STRING_ENCODING_UTF8;
    rb_rg_encode_string(&event->data.sql.provider, val, Qnil);
  } else if (symbol == rb_rg_id_host)
  {
    event->data.sql.host.encoding = RG_STRING_ENCODING_UTF8;
    rb_rg_encode_string(&event->data.sql.host, val, Qnil);
  } else if (symbol == rb_rg_id_database)
  {
    event->data.sql.database.encoding = RG_STRING_ENCODING_UTF8;
    rb_rg_encode_string(&event->data.sql.database, val, Qnil);
  } else if (symbol == rb_rg_id_query)
  {
    event->data.sql.query.encoding = RG_STRING_ENCODING_UTF8;
    rb_rg_encode_string(&event->data.sql.query, val, Qnil);
  } else if (symbol == rb_rg_id_correlation_id)
  {
    event->data.exception_thrown.correlation_id.encoding = RG_STRING_ENCODING_UTF8;
    rb_rg_encode_string(&event->data.exception_thrown.correlation_id, val, Qnil);
  } else if (symbol == rb_rg_id_url)
  {
    // Same layout as HTTTP OUT
    event->data.http_in.url.encoding = RG_STRING_ENCODING_ASCII;
    rb_rg_encode_string(&event->data.http_in.url, val, Qnil);
  } else if (symbol == rb_rg_id_verb)
  {
    // Same layout as HTTTP OUT
    event->data.http_in.verb.encoding = RG_STRING_ENCODING_ASCII;
    rb_rg_encode_short_string(&event->data.http_in.verb, val, Qnil);
  } else if (symbol == rb_rg_id_status)
  {
    // Same layout as HTTTP OUT
    event->data.http_in.status = (uint16_t)NUM2USHORT(val);
  } else if (symbol == rb_rg_id_duration)
  {
    if(event->type == RG_EVENT_SQL_INFORMATION)
    {
      event->data.sql.duration = (rg_timestamp_t)NUM2LL(val);
    } else if (event->type == RG_EVENT_HTTP_INCOMING_INFORMATION || event->type == RG_EVENT_HTTP_OUTGOING_INFORMATION)
    {
      // http_out has same layout
      event->data.http_in.duration = (rg_timestamp_t)NUM2LL(val);
    } else
    {
      rb_raise(rb_eRaygunFatal, "Invalid type for duration");
    }
  } else if (symbol == rb_rg_id_technology_type) {
      if (event->type == RG_EVENT_PROCESS_TYPE) {
         event->data.process_type.technology_type.encoding = RG_STRING_ENCODING_ASCII;
         rb_rg_encode_string(&event->data.process_type.technology_type, val, Qnil);
      } else if (event->type == RG_EVENT_BEGIN_TRANSACTION) {
         event->data.begin_transaction.technology_type.encoding = RG_STRING_ENCODING_ASCII;
         rb_rg_encode_string(&event->data.begin_transaction.technology_type, val, Qnil);
      }
  } else if (symbol == rb_rg_id_process_type) {
      if (event->type == RG_EVENT_PROCESS_TYPE) {
         event->data.process_type.process_type.encoding = RG_STRING_ENCODING_ASCII;
         rb_rg_encode_string(&event->data.process_type.process_type, val, Qnil);
      } else if (event->type == RG_EVENT_BEGIN_TRANSACTION) {
         event->data.begin_transaction.process_type.encoding = RG_STRING_ENCODING_ASCII;
         rb_rg_encode_string(&event->data.begin_transaction.process_type, val, Qnil);
      }
  } else if (symbol == rb_rg_id_api_key) {
    event->data.begin_transaction.api_key.encoding = RG_STRING_ENCODING_ASCII;
    rb_rg_encode_string(&event->data.begin_transaction.api_key, val, Qnil);
  } else {
    rb_raise(rb_eRaygunFatal, "Invalid attribute name:%s", rb_id2name(symbol));
  }
  event->length = rg_encode_size(event);
  RB_GC_GUARD(attr);
  RB_GC_GUARD(val);
  return val;
}

// The main getter interface for events. This API had it's origins at the very beginning of the project and was opaque to begin with but a
// better API would be to explicitly define the expected members to be set for a particular Event type
//
static VALUE rb_rg_event_aref(VALUE obj, VALUE attr)
{
  VALUE val = Qnil;
  rb_rg_get_event(obj);
  if (TYPE(attr) != T_SYMBOL) rb_raise(rb_eRaygunFatal, "Attribute expected to be a symbol:%s!", RSTRING_PTR(rb_obj_as_string(attr)));
  const ID symbol = SYM2ID(attr);

  if (symbol == rb_rg_id_pid)
  {
    val = ULONG2NUM(event->pid);
  } else if (symbol == rb_rg_id_tid)
  {
    val = ULONG2NUM(event->tid);
  } else if (symbol == rb_rg_id_parent_tid)
  {
    val = ULONG2NUM(event->data.thread_started.parent_tid);
  } else if (symbol == rb_rg_id_timestamp)
  {
    val = LL2NUM(event->timestamp);
  } else if (symbol == rb_rg_id_frequency)
  {
    val = LL2NUM(event->data.process_frequency.frequency);
  } else if (symbol == rb_rg_id_exception_id)
  {
    val = ULL2NUM(event->data.exception_thrown.exception_id);
  } else if (symbol == rb_rg_id_function_id)
  {
    val = UINT2NUM(event->data.function_id);
  } else if (symbol == rb_rg_id_instance_id)
  {
    val = ULL2NUM(event->data.begin.instance_id);
  } else if (symbol == rb_rg_id_method_source)
  {
    val = UINT2NUM(event->data.methodinfo.method_source);
  } else if (symbol == rb_rg_id_tailcall)
  {
    val = event->data.end.tail_call ? Qtrue : Qfalse;
  } else if (symbol == rb_rg_id_class_name)
  {
    if (event->type == RG_EVENT_METHODINFO_2) {
      val = rb_str_new(event->data.methodinfo.class_name.string, event->data.methodinfo.class_name.length);
    } else if (event->type == RG_EVENT_EXCEPTION_THROWN_2) {
      val = rb_str_new(event->data.exception_thrown.class_name.string, event->data.exception_thrown.class_name.length);
    }
  } else if (symbol == rb_rg_id_correlation_id)
  {
    val = rb_str_new(event->data.exception_thrown.correlation_id.string, event->data.exception_thrown.correlation_id.length);
  } else if (symbol == rb_rg_id_method_name)
  {
    val = rb_str_new(event->data.methodinfo.method_name.string, event->data.methodinfo.method_name.length);
  } else if (symbol == rb_rg_id_provider)
  {
    val = rb_str_new(event->data.sql.provider.string, event->data.sql.provider.length);
  } else if (symbol == rb_rg_id_host)
  {
    val = rb_str_new(event->data.sql.host.string, event->data.sql.host.length);
  } else if (symbol == rb_rg_id_database)
  {
    val = rb_str_new(event->data.sql.database.string, event->data.sql.database.length);
  } else if (symbol == rb_rg_id_query)
  {
    val = rb_str_new(event->data.sql.query.string, event->data.sql.query.length);
  } else if (symbol == rb_rg_id_url)
  {
    val = rb_str_new(event->data.http_in.url.string, event->data.http_in.url.length);
  } else if (symbol == rb_rg_id_verb)
  {
    val = rb_str_new(event->data.http_in.verb.string, event->data.http_in.verb.length);
  } else if (symbol == rb_rg_id_api_key)
  {
    val = rb_str_new(event->data.begin_transaction.api_key.string, event->data.begin_transaction.api_key.length);
  } else if (symbol == rb_rg_id_status)
  {
    val = USHORT2NUM(event->data.http_in.status);
  } else if (symbol == rb_rg_id_duration)
  {
    if(event->type == RG_EVENT_SQL_INFORMATION)
    {
      val = LL2NUM(event->data.sql.duration);
    } else if (event->type == RG_EVENT_HTTP_INCOMING_INFORMATION || event->type == RG_EVENT_HTTP_OUTGOING_INFORMATION)
    {
      // http_out has same layout
      val = LL2NUM(event->data.http_in.duration);
    } else
    {
      rb_raise(rb_eRaygunFatal, "Invalid type for duration");
    }
  } else if (symbol == rb_rg_id_technology_type) {
    if (event->type == RG_EVENT_PROCESS_TYPE) {
      val = rb_str_new(event->data.process_type.technology_type.string, event->data.process_type.technology_type.length);
    } else if (event->type == RG_EVENT_BEGIN_TRANSACTION) {
      val = rb_str_new(event->data.begin_transaction.technology_type.string, event->data.begin_transaction.technology_type.length);
    }
  } else if (symbol == rb_rg_id_process_type) {
    if (event->type == RG_EVENT_PROCESS_TYPE) {
      val = rb_str_new(event->data.process_type.process_type.string, event->data.process_type.process_type.length);
    } else if (event->type == RG_EVENT_BEGIN_TRANSACTION) {
      val = rb_str_new(event->data.begin_transaction.process_type.string, event->data.begin_transaction.process_type.length);
    }
  } else {
    rb_raise(rb_eRaygunFatal, "Invalid attribute name:%s", rb_id2name(symbol));
  }
  RB_GC_GUARD(attr);
  return val;
}

// Specific to arguments emission and BEGIN events - currently not used.
//
static VALUE rb_rg_event_arguments(int argc, VALUE *argv, VALUE obj)
{
  rg_variable_info_t arg;
  rb_rg_get_event(obj);
  // XXX guard event type to begin
  event->data.begin.argc = argc;
    for (int i = 0; i < event->data.begin.argc; i++) {
    if(i == 0) {
      arg = rb_rg_vt_coerce(rb_str_new2("host"), argv[i], Qnil);
    } else if (i == 1) {
      arg = rb_rg_vt_coerce(rb_str_new2("port"), argv[i], Qnil);
    } else {
      arg = rb_rg_vt_coerce(rb_str_new2("arg"), argv[i], Qnil);
    }
    memcpy(&event->data.begin.args[i], &arg, sizeof(rg_variable_info_t));
  }
  event->length = rg_encode_size(event);
  return Qnil;
}

// Specific to arguments emission and END events - currently not used.
//
static VALUE rb_rg_event_returnvalue(VALUE obj, VALUE args)
{
  rg_variable_info_t returnvalue;
  rb_rg_get_event(obj);
  // XXX guard event type to end
  // XXX guard argc to 1
  returnvalue = rb_rg_vt_coerce(rb_str_new2("returnValue"), args, Qnil);
  memcpy(&event->data.end.returnvalue, &returnvalue, sizeof(rg_variable_info_t));
  event->length = rg_encode_size(event);
  return Qnil;
}

// Coerces the current event to it's wire protocol equivalent event. Used in test cases for correctness assetions of interpretation of the spec
// and in production only for the HTTP IN, HTTP OUT and SQL event types.
//
VALUE rb_rg_event_encoded(VALUE obj)
{
  VALUE encoded;
  rg_byte_t *buf;
  rb_rg_get_event(obj);
  encoded = rb_str_buf_new(event->length);
  // String buffer just allocs capacity - we need to respect embedded VS heap
  // string semantics and also align length to capa.
  rb_str_set_len(encoded, event->length);

  buf = (rg_byte_t *)RSTRING_PTR(encoded);

  switch ((rg_event_type_t)event->type) {
    case RG_EVENT_BEGIN:
      rg_encode_header_impl(buf, event);
      rg_encode_begin(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_END:
      rg_encode_header_impl(buf, event);
      rg_encode_end(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_METHODINFO_2:
      rg_encode_header_impl(buf, event);
      rg_encode_methodinfo(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_SQL_INFORMATION:
      rg_encode_header_impl(buf, event);
      rg_encode_sql(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_HTTP_INCOMING_INFORMATION:
      rg_encode_header_impl(buf, event);
      rg_encode_http_in(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_HTTP_OUTGOING_INFORMATION:
      rg_encode_header_impl(buf, event);
      rg_encode_http_out(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_PROCESS_TYPE:
      rg_encode_header_impl(buf, event);
      rg_encode_process_type(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_BEGIN_TRANSACTION:
      rg_encode_header_impl(buf, event);
      rg_encode_begin_transaction(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_EXCEPTION_THROWN_2:
      rg_encode_header_impl(buf, event);
      rg_encode_exception_thrown(buf + RG_MIN_PAYLOAD, event);
      break;
    case RG_EVENT_BATCH:
      break;
    case RG_EVENT_THREAD_STARTED_2:
      rg_encode_header_impl(buf, event);
      rg_encode_thread_started(buf + RG_MIN_PAYLOAD, event);
    case RG_EVENT_THREAD_ENDED:
    case RG_EVENT_PROCESS_ENDED:
    case RG_EVENT_PROCESS_FREQUENCY:
    case RG_EVENT_END_TRANSACTION:
      memcpy(buf, event, event->length);
  }
  RB_GC_GUARD(encoded);
  return encoded;
}

// Helper for test assertions of wire protocol compatibility
VALUE rb_rg_event_length(VALUE obj)
{
  rb_rg_get_event(obj);
  return INT2NUM(event->length);
}

// Initializes the Ruby API, formal Event classes, methods and the bucket list of symbols representative to event fields
void _init_raygun_event(void)
{
  // symbol warmup
  rb_rg_id_escape = rb_intern("escape");
  rb_rg_id_pid = rb_intern("pid");
  rb_rg_id_tid = rb_intern("tid");
  rb_rg_id_timestamp = rb_intern("timestamp");
  rb_rg_id_exception_id = rb_intern("exception_id");
  rb_rg_id_function_id = rb_intern("function_id");
  rb_rg_id_instance_id = rb_intern("instance_id");
  rb_rg_id_technology_type = rb_intern("technology_type");
  rb_rg_id_process_type = rb_intern("process_type");
  rb_rg_id_class_name = rb_intern("class_name");
  rb_rg_id_method_name = rb_intern("method_name");
  rb_rg_id_tailcall = rb_intern("tailcall");
  rb_rg_id_frequency = rb_intern("frequency");
  rb_rg_id_provider = rb_intern("provider");
  rb_rg_id_host = rb_intern("host");
  rb_rg_id_database = rb_intern("database");
  rb_rg_id_query = rb_intern("query");
  rb_rg_id_duration = rb_intern("duration");
  rb_rg_id_url = rb_intern("url");
  rb_rg_id_verb = rb_intern("verb");
  rb_rg_id_status = rb_intern("status");
  rb_rg_id_method_source = rb_intern("method_source");
  rb_rg_id_api_key = rb_intern("api_key");
  rb_rg_id_correlation_id = rb_intern("correlation_id");
  rb_rg_id_parent_tid = rb_intern("parent_tid");

  // Define the distinct Ruby land event classes
  rb_cRaygunEvent = rb_define_class_under(rb_mRaygunApm, "Event", rb_cObject);
  rb_cRaygunEventBegin = rb_define_class_under(rb_cRaygunEvent, "Begin", rb_cRaygunEvent);
  rb_cRaygunEventEnd = rb_define_class_under(rb_cRaygunEvent, "End", rb_cRaygunEvent);
  rb_cRaygunEventMethodinfo = rb_define_class_under(rb_cRaygunEvent, "Methodinfo", rb_cRaygunEvent);
  rb_cRaygunEventExceptionThrown = rb_define_class_under(rb_cRaygunEvent, "ExceptionThrown", rb_cRaygunEvent);
  rb_cRaygunEventThreadStarted = rb_define_class_under(rb_cRaygunEvent, "ThreadStarted", rb_cRaygunEvent);
  rb_cRaygunEventThreadEnded = rb_define_class_under(rb_cRaygunEvent, "ThreadEnded", rb_cRaygunEvent);
  rb_cRaygunEventProcessStarted = rb_define_class_under(rb_cRaygunEvent, "ProcessStarted", rb_cRaygunEvent);
  rb_cRaygunEventProcessEnded = rb_define_class_under(rb_cRaygunEvent, "ProcessEnded", rb_cRaygunEvent);
  rb_cRaygunEventProcessType = rb_define_class_under(rb_cRaygunEvent, "ProcessType", rb_cRaygunEvent);
  rb_cRaygunEventProcessFrequency = rb_define_class_under(rb_cRaygunEvent, "ProcessFrequency", rb_cRaygunEvent);
  rb_cRaygunEventBatch = rb_define_class_under(rb_cRaygunEvent, "Batch", rb_cRaygunEvent);
  rb_cRaygunEventSql = rb_define_class_under(rb_cRaygunEvent, "Sql", rb_cRaygunEvent);
  rb_cRaygunEventHttpIn = rb_define_class_under(rb_cRaygunEvent, "HttpIn", rb_cRaygunEvent);
  rb_cRaygunEventHttpOut = rb_define_class_under(rb_cRaygunEvent, "HttpOut", rb_cRaygunEvent);
  rb_cRaygunEventBeginTransaction = rb_define_class_under(rb_cRaygunEvent, "BeginTransaction", rb_cRaygunEvent);
  rb_cRaygunEventEndTransaction = rb_define_class_under(rb_cRaygunEvent, "EndTransaction", rb_cRaygunEvent);

  // Informs the GC how to allocate the event
  rb_define_alloc_func(rb_cRaygunEvent, rb_rg_event_alloc);

  // Public API for events
  rb_define_method(rb_cRaygunEvent, "[]=", rb_rg_event_aset, 2);
  rb_define_method(rb_cRaygunEvent, "[]", rb_rg_event_aref, 1);
  rb_define_method(rb_cRaygunEvent, "encoded", rb_rg_event_encoded, 0);
  rb_define_method(rb_cRaygunEvent, "length", rb_rg_event_length, 0);
  rb_define_method(rb_cRaygunEvent, "arguments", rb_rg_event_arguments, -1);
  rb_define_method(rb_cRaygunEvent, "returnvalue", rb_rg_event_returnvalue, 1);
}
