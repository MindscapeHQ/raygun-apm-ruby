require "raygun/apm/version"

begin
  # Attempt to load a precompiled shared object (released gem)
  RUBY_VERSION =~ /(\d+\.\d+)/
  require "raygun/#{$1}/raygun_ext"
rescue LoadError
  # Attempt to load the development specific extension (non-released gem, local dev)
  require "raygun/raygun_ext"
end

require "raygun/apm/config"
require "raygun/apm/diagnostics"
require "raygun/apm/blacklist/parser"
require "raygun/apm/blacklist/translator"
require "raygun/apm/tracer"
require "raygun/apm/event"
require "raygun/apm/hooks/internals"
require "raygun/apm/hooks/net_http"
# conditionally required - may not be bundled
conditional_hooks = %w(httpclient excon mongodb)
conditional_hooks.each do |hook|
  begin
    require "raygun/apm/hooks/#{hook}"
  rescue LoadError
  end
end
