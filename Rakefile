require "bundler/gem_tasks"
require 'rake/extensiontask'
require "rake/testtask"
require 'rdoc/task'

RDoc::Task.new do |rdoc|
  rdoc.main = "README.rdoc"
  rdoc.rdoc_files.include("README.rdoc", "lib/raygun/*.rb", "lib/raygun/apm/*.rb")
end

gemspec = Gem::Specification.load('raygun-apm.gemspec')

exttask = Rake::ExtensionTask.new('raygun') do |ext|
  ext.name = 'raygun_ext'
  ext.ext_dir = 'ext/raygun'
  ext.lib_dir = 'lib/raygun'
  ext.gem_spec = gemspec
  ext.cross_compile = true
  ext.cross_platform = %w[x86-mingw32 x64-mingw32 x86-linux x86_64-linux universal-darwin]
  CLEAN.include 'tmp', 'lib/**/raygun_ext.*'
end

Rake::TestTask.new(:test) do |t|
  t.options = "-v"
  t.warning = true
  t.libs << "test"
  t.libs << "lib"
  t.test_files = FileList["test/raygun/**/*_test.rb"]
end

Rake::TestTask.new(:test_build) do |t|
  t.options = "-v"
  t.warning = true
  t.libs << "test"
  t.libs << "lib"
  t.test_files = FileList["test/raygun/**/*_test.rb"]
end

SUPPORTED_RUBY_VERSIONS = "2.5.0:2.6.0:2.7.0:3.0.0"

desc 'Beginnings of a task to trigger per ruby version and platform test suite runs on build'
task 'test_at_build' do
  SUPPORTED_RUBY_VERSIONS.split(":").each do |version|
    version =~ /(\d+\.\d+)/
    sh "cp lib/raygun/#{$1}/raygun_ext.so lib/raygun/raygun_ext.so"
    #Rake::Task["test_build"].invoke
  end
end

task 'gem:linux' do
  require 'rake_compiler_dock'
  sh "rm -Rf pkg" # ensure clean package state
  sh "bundle package"   # Avoid repeated downloads of gems by using gem files from the host.
  extra_env_vars = []
  if ENV['DEBUG']
    extra_env_vars << 'DEBUG=1'
  end 
  RakeCompilerDock.sh "bundle --local && rake clean && rake native:x86_64-linux gem RUBY_CC_VERSION=#{SUPPORTED_RUBY_VERSIONS} #{extra_env_vars.join(" ")}", platform: 'x86_64-linux', verbose: true
end

desc 'Compile native gems for distribution (Linux and Windows)'
task 'gem:native' do
  require 'rake_compiler_dock'
  sh "rm -Rf pkg" # ensure clean package state
  sh "bundle package"   # Avoid repeated downloads of gems by using gem files from the host.
  extra_env_vars = []
  if ENV['DEBUG']
    extra_env_vars << 'DEBUG=1'
  end
  exttask.cross_platform.each do |plat|
    next if plat =~ /darwin/
    # Avoid conflicting declarations of gettimeofday: https://github.com/rake-compiler/rake-compiler-dock/issues/32
    RakeCompilerDock.sh "find /usr/local/rake-compiler -name win32.h | while read f ; do sudo sed -i 's/gettimeofday/rb_gettimeofday/' $f ; done && bundle --local && rake clean && rake native:#{plat} gem RUBY_CC_VERSION=#{SUPPORTED_RUBY_VERSIONS} #{extra_env_vars.join(" ")} && rake test_at_build", platform: plat, verbose: true
  end
end

desc 'Compile native gems for distribution (Darwin)'
task 'gem:native:darwin' do
  require 'rbconfig'
  sh "rm -Rf pkg" # ensure clean package state
  config_dir = File.expand_path("~/.rake-compiler")
  config_path = File.expand_path("#{config_dir}/config.yml")
  FileUtils.mkdir_p config_dir
  open config_path, "w" do |f|
    f.puts "rbconfig-universal-darwin-2.5.0: #{ENV["HOME"]}/.rubies/ruby-2.5.5/lib/ruby/2.5.0/#{RbConfig::CONFIG["sitearch"]}/rbconfig.rb"
    f.puts "rbconfig-universal-darwin-2.6.0: #{ENV["HOME"]}/.rubies/ruby-2.6.6/lib/ruby/2.6.0/#{RbConfig::CONFIG["sitearch"]}/rbconfig.rb"
    f.puts "rbconfig-universal-darwin-2.7.0: #{ENV["HOME"]}/.rubies/ruby-2.7.2/lib/ruby/2.7.0/#{RbConfig::CONFIG["sitearch"]}/rbconfig.rb"
    f.puts "rbconfig-universal-darwin-3.0.0: #{ENV["HOME"]}/.rubies/ruby-3.0.1/lib/ruby/3.0.0/#{RbConfig::CONFIG["sitearch"]}/rbconfig.rb"
  end

  sh "bundle package"   # Avoid repeated downloads of gems by using gem files from the host.
  sh "bundle --local && rake clean && rake cross native gem RUBY_CC_VERSION=#{SUPPORTED_RUBY_VERSIONS}"
end

desc 'Run a suite of performance tests to keep tabs on overhead'
task 'perf' do
  %w(ips time memory).each do |runner|
    opts = ""
    opts << "--repeat-result median" unless runner == "memory"
    sh "benchmark-driver -v -r #{runner} #{opts} --bundler test/perf/simple*.yml"
  end
end

desc 'Run the test suite with valgrind'
task 'valgrind' do
  sh 'valgrind --trace-children=yes --trace-children-skip=*gcc*,*clang*,*git*,*dash*,*make*,*yarn*,*sed*,*readlink*,*dirname*,*node*,*sh*,*uname* --undef-value-errors=yes --track-origins=yes --read-var-info=yes --read-inline-info=yes --max-stackframe=8376928 bundle exec rake'
end

desc 'Generate a profiler design diagram'
task 'diagram' do
  sh 'dot -Tpng diagram.dot > diagram.png'
end


task :test => :compile
task :perf => :compile
task :default => :test
task "gem:native" => :clean
