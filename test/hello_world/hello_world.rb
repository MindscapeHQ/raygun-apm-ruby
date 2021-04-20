#!/usr/bin/env ruby
libdir = File.expand_path(File.join(File.dirname(__FILE__), '../../lib/'))
$:.unshift libdir
require 'raygun/apm'

thread_count = 5
thread_requests = 5
$sql_query_count = 100

class Hello
  def initialize(tracer)
    @tracer = tracer
  end
  def world
    tracer = @tracer
    event = Raygun::Apm::Event::HttpIn.new
    event[:pid] = Process.pid
    event[:tid] = 0
    event[:timestamp] = tracer.now
    event[:url] = 'https://google.com/'
    event[:verb] = 'GET'
    event[:status] = 200
    event[:duration] = 1000  
    tracer.emit(event)

    $sql_query_count.times do |i|
      sleep 0.001
      event = Raygun::Apm::Event::Sql.new
      event[:pid] = Process.pid
      event[:tid] = 0
      event[:timestamp] = tracer.now
      event[:provider] = 'postgres'
      event[:host] = 'localhost'
      event[:database] = 'rails'
      event[:query] = 'SELECT * from FOO;'
      event[:duration] = 1000
      tracer.emit(event)
    end
  end
end

threads = []

tracer = Raygun::Apm::Tracer.new
tracer.udp_sink!
thread_count.times do
  threads << Thread.new do
    thread_requests.times do
      tracer.start_trace
      hw = Hello.new(tracer)
      hw.world
      tracer.end_trace
    end
  end
end
threads.map(&:join)
tracer.process_ended
