#include "raygun_ext.h"

VALUE rb_mRaygun;
VALUE rb_mRaygunApm;

// The main extension initializer called by the Ruby VM (Init_* convetion)
void Init_raygun_ext(void)
{
  // Public Ruby API
  rb_mRaygun = rb_define_module("Raygun");
  rb_mRaygunApm = rb_define_module_under(rb_mRaygun, "Apm");

  // Initis all the other Ruby Profiler concerns
  _init_raygun_coercion();
  _init_raygun_tracer();
  _init_raygun_event();
  _init_raygun_ringbuf();
  _init_raygun_errors();
}
