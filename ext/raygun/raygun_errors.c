#include "raygun_coercion.h"

// Main tracer specific error we handle by stopping it
VALUE rb_eRaygunFatal;

void _init_raygun_errors(void){
  rb_eRaygunFatal = rb_define_class_under(rb_mRaygunApm, "FatalError", rb_eStandardError);
}
