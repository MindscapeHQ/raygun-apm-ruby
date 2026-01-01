# encoding: utf-8

# Makefile generator helper - from standard library
require 'mkmf'

# References core headers extracted by Ruby minor version in https://github.com/os97673/debase-ruby_core_source
# Required for some of the lower level profiler features (vm_core.h, rb_thread_t, etc.)
begin
  require 'debase/ruby_core_source'
rescue LoadError => e
  STDERR.puts "=" * 70
  STDERR.puts "Raygun APM: Failed to load debase-ruby_core_source"
  STDERR.puts ""
  STDERR.puts "This gem is required to compile the native extension against Ruby VM internals."
  STDERR.puts "Please ensure debase-ruby_core_source >= 3.3.6 is installed:"
  STDERR.puts ""
  STDERR.puts "  gem install debase-ruby_core_source"
  STDERR.puts ""
  STDERR.puts "Error: #{e.message}"
  STDERR.puts "=" * 70
  exit(1)
end

# Verify we have headers for the current Ruby version
ruby_version = "#{RUBY_VERSION}"
STDERR.puts "[Raygun APM] Building native extension for Ruby #{ruby_version}"

headers = proc do
  have_header('ruby.h') &&
  have_header('ruby/debug.h') &&
  have_header("vm_core.h")
end

dir_config('raygun')

# To allow for swapping out the compiler - clang in favour of gcc for example
RbConfig::MAKEFILE_CONFIG['CC'] = ENV['CC'] if ENV['CC']

# Pedantic about all the things
append_cflags '-pedantic'
append_cflags '-Wall'
append_cflags '-Werror'
append_cflags '-std=c99'
append_cflags '-std=gnu99'
append_cflags '-fdeclspec'
append_cflags '-fms-extensions'
append_cflags '-ggdb3'
# Disable warnings that cause issues with third-party code (rax, bipbuffer) on 64-bit platforms
# These are safe truncations for buffer sizes that won't exceed 32-bit limits
append_cflags '-Wno-shorten-64-to-32'
# Clang-specific: disable unknown warning option errors for GCC-only pragmas in third-party code
append_cflags '-Wno-unknown-warning-option'
# Disable const qualifier warnings in third-party rax.c debug code
append_cflags '-Wno-incompatible-pointer-types-discards-qualifiers'
# Enables additional flags, stack protection and debug symbols
if ENV['DEBUG']
  have_library 'ssp'
  have_func '__stack_chk_guard'
  have_func '__stack_chk_fail'
  append_cflags '-ggdb3'
  # Needed reduced -On levels for SSP to do its job.
  # Used to be -O0 but switched to -Og since -O0 disables
  # some optimizations that debug tools need.
  # https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
  append_cflags '-Og'
  append_cflags '-fstack-protector-all'
  append_cflags '-DRB_RG_DEBUG'
else
  append_cflags '-O3'
end

# Renders an ASCII presentation of the shadow stack at runtime
if ENV['DEBUG_SHADOW_STACK']
  append_cflags '-DRB_RG_DEBUG_SHADOW_STACK'
end

unless create_header
  STDERR.print("extconf.h creation failed\n")
  exit(1)
end

# Check for the presence of headers in ruby_core_headers for the version currently compiled for
unless Debase::RubyCoreSource.create_makefile_with_core(headers, 'raygun_ext')
  STDERR.puts "=" * 70
  STDERR.puts "Raygun APM: Makefile creation failed"
  STDERR.puts ""
  STDERR.puts "One or more Ruby VM headers (vm_core.h) were not found for Ruby #{ruby_version}."
  STDERR.puts ""
  STDERR.puts "This usually means debase-ruby_core_source does not yet have headers"
  STDERR.puts "for your Ruby version. Please try updating the gem:"
  STDERR.puts ""
  STDERR.puts "  gem update debase-ruby_core_source"
  STDERR.puts ""
  STDERR.puts "If the problem persists, please report this issue at:"
  STDERR.puts "  https://github.com/MindscapeHQ/raygun-apm-ruby/issues"
  STDERR.puts "=" * 70
  exit(1)
end

STDERR.puts "[Raygun APM] Native extension configured successfully"