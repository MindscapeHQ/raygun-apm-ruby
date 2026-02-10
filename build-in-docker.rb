#!/usr/bin/env ruby
# Script to build native gems using Docker directly (bypasses rake-compiler-dock path checks)

require 'fileutils'

# Platforms to build (excluding darwin which requires macOS)
# Using versions supported by debase-ruby_core_source
# Note: 3.3.10+ and 3.4.8 may fail if debase-ruby_core_source lacks headers
PLATFORMS = {
  'x86_64-linux' => '3.0.7:3.1.7:3.2.9',
  'x86-linux' => '3.0.7:3.1.7:3.2.9',
  'x64-mingw-ucrt' => '3.1.7:3.2.9',
  'x64-mingw32' => '3.0.7',
  'x86-mingw32' => '3.0.7',
}

IMAGE_VERSION = '1.11.1'

FileUtils.rm_rf 'pkg'

# Note: run "bundle package" manually before this script if needed

PLATFORMS.each do |platform, ruby_versions|
  puts "\n" + "="*60
  puts "Building for #{platform}"
  puts "="*60

  image = "ghcr.io/rake-compiler/rake-compiler-dock-image:#{IMAGE_VERSION}-mri-#{platform}"
  
  # Pull the image
  system("docker pull #{image}") or warn "Could not pull #{image}"
  
  # Convert Windows path to Unix-style for Docker
  pwd = Dir.pwd.gsub(/^([a-z]):/i) { "/#{$1.downcase}" }
  
  # Fix gettimeofday conflict for Windows builds
  win_fix = platform =~ /mingw/ ? 'find /usr/local/rake-compiler -name win32.h -exec sudo sed -i "s/gettimeofday/rb_gettimeofday/" {} \\; && ' : ""
  
  cmd = [
    'docker', 'run', '--rm',
    '-v', "#{Dir.pwd}:#{pwd}",
    '-w', pwd,
    '-e', "RUBY_CC_VERSION=#{ruby_versions}",
    image,
    'bash', '-c',
    "#{win_fix}bundle install --local && rake clean && rake native:#{platform} gem RUBY_CC_VERSION=#{ruby_versions}"
  ]
  
  puts "Running: #{cmd.join(' ')}"
  success = system(*cmd)
  
  unless success
    warn "Build failed for #{platform}"
  end
end

puts "\n" + "="*60
puts "Build complete! Check pkg/ directory for gems."
puts "="*60
