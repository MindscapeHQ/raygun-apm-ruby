#include "raygun_ringbuf.h"

// Wraps https://github.com/willemt/bipbuffer - we don't use this in production paths currently, but wrapped the implementation if there's a need to
// have the bipbuffer exposed to Ruby

VALUE rb_cRaygunRingbuf;

// Initializes a Raygun::Apm::Ringbuf instance with a specific buffer capacity
VALUE rb_rg_ringbuf_initialize(VALUE obj, VALUE capacity)
{
    bipbuf_t *bipbuf = NULL;
    uint32_t cap = NUM2UINT(capacity);
    rb_rg_get_ringbuf(obj);
    bipbuf = bipbuf_new(cap);
    if (!bipbuf) rb_raise(rb_eRaygunFatal, "Could not alloc UDP sender bipbuf");
    ringbuf->bipbuf = bipbuf;
    return Qtrue;
}

// Returns the capacity of the bipbuf as a Ruby Fixnum
VALUE rb_rg_ringbuf_capacity(VALUE obj)
{
    rb_rg_get_ringbuf(obj);
    return UINT2NUM(bipbuf_size(ringbuf->bipbuf));
}

// Pushes a chunk of bytes (Ruby String typically)
VALUE rb_rg_ringbuf_push(VALUE obj, VALUE input_bytes)
{
    int retval;
    rb_rg_get_ringbuf(obj);
    retval = bipbuf_offer(ringbuf->bipbuf, (unsigned char *)RSTRING_PTR(input_bytes), (int)RSTRING_LEN(input_bytes));
    RB_GC_GUARD(input_bytes);
    if (retval > 0)
    {
        return Qtrue;
    } else
    {
        return Qfalse;
    }
}

// Shifts a chunk of bytes of a specific length
VALUE rb_rg_ringbuf_shift(VALUE obj, VALUE length)
{
    unsigned char *bytes;
    unsigned int len;
    rb_rg_get_ringbuf(obj);
    len = NUM2UINT(length);

    bytes = bipbuf_poll(ringbuf->bipbuf, len);
    if(bytes)
    {
        return rb_str_new((char *)bytes, len);
    } else
    {
        return Qnil;
    }
}

// The main GC callback from the typed data (https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-) struct.
// Typically it's the responsibility of this method to walk all struct members with data to free so that at the end of this function, if we free the events
// struct, there's nothing dangling about on the heap.
//
void rg_ringbuf_free(void *ptr)
{
    rg_ringbuf_t *ringbuf = (rg_ringbuf_t *)ptr;
    if (!ringbuf) return;
    if(ringbuf->bipbuf)
    {
        bipbuf_free(ringbuf->bipbuf);
    }
    xfree(ringbuf);
    ringbuf = NULL;
}

// Used by ObjectSpace to estimate the size of a Ruby object. This needs to account for all the retained memory of the object and requires walking any
// collection specific struct members and anything else malloc heap allocated.
//
size_t rg_ringbuf_sizeof(const void *ptr)
{
    rg_ringbuf_t *ringbuf = (rg_ringbuf_t *)ptr;
    return sizeof(rg_ringbuf_t) + sizeof(bipbuf_t) + ringbuf->bipbuf->size;
}

// The main typed data struct that helps to inform the VM (mostly the GC) on how to handle a wrapped structure
// References https://github.com/ruby/ruby/blob/master/doc/extension.rdoc#encapsulate-c-data-into-a-ruby-object-
//
// We only wrap the raw bipbuf struct which knows NOTHING about Ruby and as such the mark callback is
// empty as there's nothing to let the Ruby GC know about with regards to object reachability.
//
const rb_data_type_t rb_rg_ringbuf_type = {
    .wrap_struct_name = "rb_rg_ringbuf",
    .function = {
        .dmark = NULL,
        .dfree = rg_ringbuf_free,
        .dsize = rg_ringbuf_sizeof,
    },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

// Allocation helper for allocating a blank bipbuf struct and let a Ruby land object wrap the allocated struct.
//
// Nothing fancy here - just a direct wrap of rg_ringbuf_t struct to a Raygun::Apm::Ringbuf instance
//
static VALUE rb_rg_ringbuf_alloc(VALUE klass)
{
  rg_ringbuf_t *ringbuf = ALLOC(rg_ringbuf_t);
  return TypedData_Wrap_Struct(klass, &rb_rg_ringbuf_type, ringbuf);
}

// Init helper, called when raygun_ext.so is loaded
void _init_raygun_ringbuf()
{

    // Define the class
    rb_cRaygunRingbuf = rb_define_class_under(rb_mRaygunApm, "Ringbuf", rb_cData);

    // Custom allocator
    rb_define_alloc_func(rb_cRaygunRingbuf, rb_rg_ringbuf_alloc);

    // Define the methods
    rb_define_method(rb_cRaygunRingbuf, "initialize", rb_rg_ringbuf_initialize, 1);
    rb_define_method(rb_cRaygunRingbuf, "capacity", rb_rg_ringbuf_capacity, 0);
    rb_define_method(rb_cRaygunRingbuf, "push", rb_rg_ringbuf_push, 1);
    rb_define_method(rb_cRaygunRingbuf, "shift", rb_rg_ringbuf_shift, 1);
}