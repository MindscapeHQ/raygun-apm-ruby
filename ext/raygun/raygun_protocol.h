#ifndef RAYGUN_PROTOCOL_H
#define RAYGUN_PROTOCOL_H

// Data layout of the wire protocol.
//

#include <inttypes.h>

// Features
#define RG_ENCODE_ASYNC 1

// Wire protocol specific max and minimum values
#define RG_MAX_STRING_SIZE 4096
#define RG_MAX_SHORT_STRING_SIZE 127
#define RG_MIN_PAYLOAD 19
#define RG_MAX_VARIABLE_NAME 200
#define RG_MAX_ARGS_LENGTH 16
// Timer thread tick interval in seconds
#define RG_TIMER_THREAD_TICK_INTERVAL 1
// How long to suspend the sink thread for if there's no data to send (microseconds)
#define RG_SINK_THREAD_TICK_INTERVAL 100000
// After how many ticks (seconds) to re-sync the methodinfo table
#define RG_TIMER_THREAD_METHODINFO_TICK 30
// UDP sink ring buffer size - what the encoder encodes too and the dispatcher feeds from
#define RG_RINGBUF_SIZE 10 * 1024 * 1024
// First wrapper system frame for any given trace is constant function ID 1
#define RG_TRACE_ENTRYPOINT_FRAME_ID 1
#define RG_TRACE_ENTRYPOINT_FRAME_NAME "Ruby_APM_profiler_trace";
// Blacklist specific
#define RG_BLACKLIST_UNLISTED 0
#define RG_BLACKLIST_WHITELISTED 1
#define RG_BLACKLIST_BLACKLISTED 2
#define RG_BLACKLIST_WHITELISTED_NAMESPACE 3
#define RG_BLACKLIST_BLACKLISTED_NAMESPACE 4
// Shadow thread specific
#define RG_SHADOW_STACK_LIMIT 256
#define RG_THREAD_FRAMELESS -1
#define RG_THREAD_ORPHANED 0

// Max scratch buffer size - this is an intermediate static buffer that the encoder encodes to to facilitate the 0 alloc implementation
#define RG_ENCODER_SCRATCH_BUFFER_SIZE 32 * 1024
// Size of BATCH packet.
// For UDP consider MTU issues (1400 to account for switch buffers etc.)
// By setting this to 0 we're effectively sending all data unbatched.
#define RG_BATCH_PACKET_SIZE 1400
// Special case static buffer size for large events (SQL mostly) that exceeds RG_BATCH_PACKET_SIZE that we'd still pack into a sequenced batch
#define RG_MAX_BATCH_PACKET_SIZE 4096
#define RG_BATCH_HEADLEN 13
#define RG_SHUTDOWN_GRACE_SECONDS 5
#define RG_MAX_BLACKLIST_NEEDLE_SIZE RG_MAX_STRING_SIZE + 1 + RG_MAX_STRING_SIZE + 1

// Async encoder
#ifdef RG_ENCODE_ASYNC
#define RG_ENCODE_ASYNC_BUF_SIZE 1 * 1024 * 1024
#define RG_ASYNC_ENCODER_THREAD_TICK_INTERVAL 10000
#endif

// supported types

typedef enum
{
  RG_VT_VOID = 0x1,
  RG_VT_BOOLEAN = 0x2,
  RG_VT_BYTE = 0x3,
  RG_VT_SHORT = 0x4,
  RG_VT_UNSIGNED_SHORT = 0x5,
  RG_VT_INT32 = 0x6,
  RG_VT_UNSIGNED_INT32 = 0x7,
  RG_VT_LONG = 0x8,
  RG_VT_UNSIGNED_LONG = 0x9,
  RG_VT_FLOAT = 0xa,
  RG_VT_DOUBLE = 0xb,
  RG_VT_STRING = 0xc,
  RG_VT_STRUCT = 0xd,
  RG_VT_OBJECT = 0xe,
  RG_VT_ARRAY = 0xf,
  RG_VT_EMPTYSTRING = 0x10,
  RG_VT_NULLSTRING = 0x11,
  RG_VT_NULLOBJECT = 0x12,
  RG_VT_LARGESTRING = 0x13,
  /* TODO: Not defined in spec yet, required to coerce Bignum */
  //RG_VT_LONG_LONG = 0x14,e
  //RG_VT_UNSIGNED_LONG_LONG = 0x15,
} rg_variable_t;

typedef enum {
  RG_STRING_ENCODING_NULL = 0,
  RG_STRING_ENCODING_UTF_16LE = 0x1,
  RG_STRING_ENCODING_UTF_16BE = 0x2,
  RG_STRING_ENCODING_ASCII = 0x3,
  RG_STRING_ENCODING_UTF7 = 0x4,
  RG_STRING_ENCODING_UTF8 = 0x5,
  RG_STRING_ENCODING_UTF32LE = 0x6

} rg_string_encoding_t;

// internal types

typedef int64_t rg_timestamp_t;
typedef uint64_t rg_frequency_t;
typedef uint32_t rg_pid_t;
typedef uint32_t rg_tid_t;
typedef uint64_t rg_exception_instance_id_t;
typedef uint32_t rg_function_id_t;
typedef uint64_t rg_instance_id_t;
typedef int16_t rg_length_t;
typedef uint32_t rg_sequence_t;
typedef uint8_t rg_argc_t;

// variable type specific

typedef uint64_t rg_void_t;
typedef int8_t rg_boolean_t;
typedef uint8_t rg_byte_t;
typedef int16_t rg_short_t;
typedef uint16_t rg_unsigned_short_t;
typedef int32_t rg_int_t;
typedef uint32_t rg_unsigned_int_t;
typedef int64_t rg_long_t;
typedef uint64_t rg_unsigned_long_t;
typedef long long rg_long_long_t;
typedef unsigned long long rg_unsigned_long_long_t;
typedef float rg_float_t;
typedef double rg_double_t;

typedef struct _rg_encoded_short_string_t {
  rg_byte_t encoding;
  rg_byte_t length;
  char string[UINT8_MAX];
} rg_encoded_short_string_t;

typedef struct _rg_encoded_string_t {
  rg_byte_t encoding;
  rg_length_t length;
  char string[RG_MAX_STRING_SIZE];
} rg_encoded_string_t;

// TODO: object
// TODO: struct

typedef struct {
  rg_byte_t type;
  rg_byte_t length;
  void *members;
} rg_array_t;

// TODO: emptystring, nullstring, nullobject represent any specific value @ all?

typedef struct _rg_largestring_t {
  uint32_t length;
  char string[RG_MAX_STRING_SIZE];
} rg_largestring_t;

typedef struct _rg_void_return_t {
  rg_length_t length;
  rg_byte_t type;
  rg_byte_t name_length;
} rg_void_return_t;

typedef struct _rg_variable_info_t {
  rg_length_t length;
  rg_byte_t type;
  rg_byte_t name_length;
  char name[RG_MAX_VARIABLE_NAME];
  // TODO: settle on a better naming convention - we cannot use type names such as void, long etc. as
  // struct member names
  union {
    rg_void_t t_void;
    rg_boolean_t t_boolean;
    rg_byte_t t_byte;
    rg_short_t t_short;
    rg_unsigned_short_t t_unsigned_short;
    rg_int_t t_int32;
    rg_unsigned_int_t t_unsigned_int32;
    rg_long_t t_long;
    rg_unsigned_long_t t_unsigned_long;
    // Ruby Float is backed by C double - revisit
    rg_float_t t_float;
    rg_double_t t_double;
    rg_encoded_string_t t_encoded_string;
    // TODO: object
    // TODO: struct
    rg_array_t t_array;
    rg_largestring_t t_largestring;
    // TODO: Not yet supported by spec, for Bignum
    rg_long_long_t t_long_long;
    rg_unsigned_long_long_t t_unsigned_long_long;
  } as;
} rg_variable_info_t;

// Internal wrappers

// Represents a whitelisted method that we shadow - a methodinfo table entry

typedef struct _rg_method_t {
  rg_function_id_t function_id;
  size_t encoded_size;
  rg_byte_t *encoded;
  rg_byte_t source;
  char *name;
  int length;
} rg_method_t;

// Represents a shadow thread that observes the execution state of a Ruby thread

typedef struct _rg_thread_t {
  // The thread ID used in wire protocol commands
  rg_tid_t tid;
  // For thread anchestry tracking
  rg_tid_t parent_tid;
  // The current depth of the shadow stack
  rg_int_t shadow_top;
  // The current depth of the Ruby VM stack - this in practice can exceed the shadow stack as we only follow a limited amount of frames
  rg_int_t vm_top;
  // Optimization to not follow library frames to deep
  rg_int_t level_deep_into_third_party_lib;
  rg_function_id_t shadow_stack[RG_SHADOW_STACK_LIMIT];
} rg_thread_t;

// Event structs to feed process state to the agent. We know in the spec they are represented as commands, but for the profiler we prefered to
// refer to them as events as they represent "observed events of interest", whereas a command is more "do something". The Encoder is 0 alloc
// and these structs carry intermediate data before carefully encoded into the ring buffer that feeds the dispatcher sink.

typedef enum _rg_event_type_t {
  RG_EVENT_BEGIN = 0x1,
  RG_EVENT_END = 0x2,
  RG_EVENT_METHODINFO_2 = 0xf,
  RG_EVENT_EXCEPTION_THROWN_2 = 0x12,
  RG_EVENT_THREAD_ENDED = 0x8,
  RG_EVENT_PROCESS_ENDED = 0xa,
  RG_EVENT_PROCESS_FREQUENCY = 0xb,
  RG_EVENT_PROCESS_TYPE = 0xc,
  RG_EVENT_BATCH = 0xfa,
  // Extended events introduced after the intial wire protocol document
  RG_EVENT_SQL_INFORMATION = 0x64,
  RG_EVENT_HTTP_INCOMING_INFORMATION = 0x65,
  RG_EVENT_HTTP_OUTGOING_INFORMATION = 0x66,
  // Trace boundaries
  RG_EVENT_BEGIN_TRANSACTION = 0x10,
  RG_EVENT_END_TRANSACTION = 0x11,
  // Thread ancestry support
  RG_EVENT_THREAD_STARTED_2 = 0x13
} rg_event_type_t;

// The type of whitelisted method instrumented - most would be user code or system

typedef enum _rg_method_source_t {
  RG_METHOD_SOURCE_USER_CODE = 0x0,
  RG_METHOD_SOURCE_SYSTEM = 0x1,
  RG_METHOD_SOURCE_KNOWN_LIBRARY = 0x2,
  RG_METHOD_SOURCE_WAIT_FOR_USER_INPUT = 0x3,
  RG_METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION = 0x4,
  RG_METHOD_SOURCE_JIT_COMPILATION = 0x5,
  RG_METHOD_SOURCE_GARBAGE_COLLECTION = 0x6,
} rg_method_source_t;

// Event specific protocol frames

// RG_EVENT_EXCEPTION_THROWN_2

typedef struct _rg_event_exception_thrown_2_t {
  rg_exception_instance_id_t exception_id;
  rg_encoded_string_t class_name;

  rg_encoded_string_t correlation_id;
} rg_event_exception_thrown_2_t;

// RG_EVENT_METHODINFO_2

typedef struct _rg_event_methodinfo_2_t {
  rg_function_id_t function_id;
  rg_encoded_string_t class_name;
  rg_encoded_string_t method_name;
  rg_byte_t method_source;
} rg_event_methodinfo_2_t;

// RG_EVENT_PROCESS_FREQUENCY

typedef struct _rg_event_process_frequency_t {
  rg_frequency_t frequency;
} rg_event_process_frequency_t;

// RG_EVENT_PROCESS_TYPE

typedef struct _rg_event_process_type_t {
  rg_encoded_string_t technology_type;
  rg_encoded_string_t process_type;
} rg_event_process_type_t;

// RG_EVENT_BEGIN

typedef struct rg_event_begin_t {
  rg_function_id_t function_id;
  rg_instance_id_t instance_id;
  rg_argc_t argc;
#ifdef RB_RG_EMIT_ARGUMENTS
  rg_variable_info_t args[RG_MAX_ARGS_LENGTH];
#else
  // Dummy for API compatibility in consumers of this struct
  rg_variable_info_t *args;
#endif
} rg_event_begin_t;

// RG_EVENT_END

typedef struct  rg_event_end_t {
  rg_function_id_t function_id;
  rg_boolean_t tail_call;
#ifdef RB_RG_EMIT_ARGUMENTS
  rg_variable_info_t returnvalue;
#else
  // Dummy for API compatibility in consumers of this struct
  rg_void_return_t returnvalue;
#endif
} rg_event_end_t;

// Extended events - these were introduced for the Ruby and Node profilers as it's a lot cheaper observing and populating these at source than to
// coerce method arguments and return values and fish them out Agent side.

// RG_EVENT_SQL_INFORMATION

typedef struct _rg_event_sql_t {
  rg_encoded_string_t provider;
  rg_encoded_string_t host;
  rg_encoded_string_t database;
  rg_encoded_string_t query;
  rg_timestamp_t duration;
} rg_event_sql_t;

// RG_EVENT_HTTP_INCOMING_INFORMATION

typedef struct _rg_event_http_in_t {
  rg_encoded_string_t url;
  rg_encoded_short_string_t verb;
  uint16_t status;
  rg_timestamp_t duration;
} rg_event_http_in_t;

// RG_EVENT_HTTP_OUTGOING_INFORMATION

typedef struct _rg_event_http_out_t {
  rg_encoded_string_t url;
  rg_encoded_short_string_t verb;
  uint16_t status;
  rg_timestamp_t duration;
} rg_event_http_out_t;

// Trace boundary specific - implemented Agent side to treat Ruby events as a stream and let the profiler explicitly mark the start and end of it

// RG_EVENT_BEGIN_TRANSACTION

typedef struct _rg_event_begin_transaction_t {
  rg_encoded_string_t api_key;
  rg_encoded_string_t technology_type;
  rg_encoded_string_t process_type;
} rg_event_begin_transaction_t;

// RG_EVENT_THREAD_STARTED_2

typedef struct _rg_event_thread_started_t {
  rg_tid_t parent_tid;
} rg_event_thread_started_t;

// RG_EVENT_BATCH

// Only one of these exist per profiler instance, per process

typedef struct _rg_event_batch_t {
  rg_length_t length;
  rg_byte_t type;
  rg_length_t count;
  rg_sequence_t sequence;
  rg_pid_t pid;
  rg_byte_t buf[RG_MAX_BATCH_PACKET_SIZE];
} rg_event_batch_t;

// A generic container for an Event that includes the wire protocol header fields and also allows for representing any type through the data member union

typedef struct _rg_event_t {
  rg_length_t length;
  rg_byte_t type;
  rg_pid_t pid;
  rg_tid_t tid;
  rg_timestamp_t timestamp;
  union {
    // event structs
    rg_event_exception_thrown_2_t exception_thrown;
    rg_event_methodinfo_2_t methodinfo;
    rg_event_process_frequency_t process_frequency;
    rg_event_process_type_t process_type;
    rg_event_begin_t begin;
    rg_event_end_t end;
    rg_event_sql_t sql;
    rg_event_http_in_t http_in;
    rg_event_http_out_t http_out;
    rg_event_begin_transaction_t begin_transaction;
    rg_event_thread_started_t thread_started;

    // polymorphic members suitable for more than one event
    rg_function_id_t function_id;
  } data;
} rg_event_t;

#endif
