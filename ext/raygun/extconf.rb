# encoding: utf-8

# Makefile generator helper - from standard library
require 'mkmf'
# References core headers extracted by Ruby minor version in https://github.com/os97673/debase-ruby_core_source . Required for some of the lower level profiler features
require 'debase/ruby_core_source'

headers = proc do
  have_header('ruby.h') &&
  have_header('ruby/debug.h') &&
  have_header("vm_core.h")
end

dir_config('raygun')

# To allow for swapping out the compiler - clang in favour of gcc for example
RbConfig::MAKEFILE_CONFIG['CC'] = ENV['CC'] if ENV['CC']

# Ensure proper architecture flags for ARM64
if RUBY_PLATFORM =~ /arm64|aarch64/
  append_cflags '-arch arm64'
elsif RUBY_PLATFORM =~ /x86_64/
  append_cflags '-arch x86_64'
end

# Pedantic about all the things
append_cflags '-pedantic'
append_cflags '-Wall'
append_cflags '-Werror'
append_cflags '-Wno-error=shorten-64-to-32'
append_cflags '-Wno-error=incompatible-pointer-types-discards-qualifiers'
append_cflags '-Wno-error=unknown-warning-option'
append_cflags '-Wno-shorten-64-to-32'
append_cflags '-Wno-error=unused-but-set-variable'

# Ruby 2.7's rb_intern macro implementation triggers -Wcompound-token-split-by-macro warnings
# with Clang 12+ due to RUBY_CONST_ID_CACHE using statement expressions across macro boundaries.
# This was fixed in Ruby 3.0+. See:
# - https://bugs.ruby-lang.org/issues/17865
# - https://github.com/ruby/ruby/commit/7e8a9af9db42a21f6a1125a29e98c45ff9d5833b
if RUBY_VERSION.start_with?('2.7')
  append_cflags '-Wno-error=compound-token-split-by-macro'
end

append_cflags '-std=c99'
append_cflags '-std=gnu99'
append_cflags '-fdeclspec'
append_cflags '-fms-extensions'
append_cflags '-ggdb3'
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
  STDERR.print("Makefile creation failed\n")
  STDERR.print("One or more ruby headers not found\n")
  exit(1)
end