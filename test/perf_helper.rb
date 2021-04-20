$LOAD_PATH.unshift File.expand_path("../../lib", __FILE__)
require "raygun/apm"
require 'subject'

def rails_prelude(tracer_enabled: false)
  orig_gemfile = ENV["BUNDLE_GEMFILE"]
  rails_path = File.expand_path(File.join(File.dirname(orig_gemfile), 'test', 'rails_5.2.2'))
  $LOAD_PATH.unshift rails_path
  rails_gemfile = File.join(rails_path, 'Gemfile')
  ENV["BUNDLE_GEMFILE"] = rails_gemfile
  bundler_gemspec = Gem.loaded_specs['bundler']
  bundler_setup_rb = File.join(bundler_gemspec.gem_dir+'/lib/bundler/setup.rb')
  Bundler.reset!
  require bundler_setup_rb || raise("Failed to reload bundler")
  ENV['RAYGUN_SKIP_MIDDLEWARE'] = "1" unless tracer_enabled
  require "config/environment"
  app = Rails.application
  ActionDispatch::Integration::Session.new(app)
end