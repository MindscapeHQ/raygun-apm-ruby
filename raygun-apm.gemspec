
require File.expand_path('../lib/raygun/apm/version', __FILE__)

Gem::Specification.new do |spec|
  spec.name          = "raygun-apm"
  spec.version       = Raygun::Apm::VERSION

  spec.authors       = ["Raygun Limited"]
  spec.email         = ["ruby-apm@raygun.io", "support@raygun.com"]

  spec.summary       = %q{Raygun application performance monitoring core Profiler}
  spec.homepage      = "https://raygun.com/platform/apm"
  spec.license       = "MIT"

  spec.files         = Dir['README.rdoc', 'raygun-apm.gemspec', 'LICENSE', 'LICENSE.bipbuffer', 'COPYING.rax', 'lib/**/*', 'bin/**/*'].reject { |f| f.match(/raygun_ext\./) }
  spec.files         += Dir['lib/raygun/**/{2}*/raygun_ext.*']
  spec.bindir        = "bin"
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.require_paths = ["lib", "ext"]

  spec.platform = Gem::Platform::RUBY

  spec.extensions = ["ext/raygun/extconf.rb"]
  spec.required_ruby_version = '>= 2.5.0'

  spec.add_development_dependency "debase-ruby_core_source", "~> 3.4.1"
  spec.add_development_dependency "bundler", "~> 2.2.15"
  spec.add_development_dependency "rake", "~> 13.0.3"
  spec.add_development_dependency "minitest", "~> 5.14.4"
  spec.add_development_dependency "rake-compiler", "~> 1.1.1"
  spec.add_development_dependency "rake-compiler-dock", "~> 1.2.1"
  spec.add_development_dependency "benchmark_driver", "~> 0.15.9"
  spec.add_development_dependency "faraday", "~> 1.0.1"
  spec.add_development_dependency "multipart-post", "~> 2.1.1"
  spec.add_development_dependency "rest-client", "~> 2.1.0"
  spec.add_development_dependency "excon", "~> 0.73.0"
  spec.add_development_dependency "httparty", "~> 0.18.0"
  spec.add_development_dependency "httpclient", "~> 2.8.3"
  spec.add_development_dependency "mongoid", "~> 7.1.2"
end
