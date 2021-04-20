$LOAD_PATH.unshift File.expand_path("../../lib", __FILE__)
require "raygun/apm"

require "minitest/autorun"

require 'subject'
require 'socket'

class Raygun::Test < Minitest::Test
  def apm_trace
    tracer = Raygun::Apm::Tracer.new
    Raygun::Apm::Tracer.instance = tracer
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end
    tracer.start_trace
    yield
  ensure
    tracer.end_trace
    Raygun::Apm::Tracer.instance = nil
    return events
  end
end

module MiniTest::Assertions
  def assert_fatal_error(message_matcher = nil)
    exception = assert_raises(Raygun::Apm::FatalError) { yield }
    if message_matcher
      assert_match(message_matcher, exception.message)
    end
  end
end

# To ensure the exit status code is properly propagated and respected for failing assertions
module Kernel
  alias :__at_exit :at_exit
  def at_exit(&block)
    __at_exit do
      exit_status = $!.status if $!.is_a?(SystemExit)
      block.call
      exit exit_status if exit_status
    end
  end
end
