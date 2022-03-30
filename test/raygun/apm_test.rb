require "test_helper"
require 'rbconfig/sizeof'
require 'excon'

class Raygun::ApmTest < Raygun::Test

  def setup
    @subject = Subject.new
  end

  def test_unicode_methods
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    🐄 = @subject.🔪
    @subject.★★★
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}

    assert_equal "Subject", events[2][:class_name]
    assert_equal "?", events[2][:method_name]
    assert_equal "Subject", events[5][:class_name]
    assert_equal "???", events[5][:method_name]
  end

  def test_synchronization_source
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    sleep 1
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}
    assert_equal "Object", events[2][:class_name]
    assert_equal "sleep", events[2][:method_name]
    assert_equal Raygun::Apm::Tracer::METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION, events[2][:method_source]
  end

  def test_mutex_events_reported_when_not_inside_third_party_code
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.debug_blacklist = true
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    semaphore = Mutex.new
    Thread.new do
      semaphore.synchronize do
        sleep 0.01
        sleep 0.01
      end
    end.join
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::ThreadStarted,
      #mutex.synchronise
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::Methodinfo,
      #sleep 1
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      #sleep 2
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      #end mutex.synchronize
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}
    assert_equal "Thread::Mutex", events[3][:class_name]
    assert_equal "synchronize", events[3][:method_name]
    assert_equal Raygun::Apm::Tracer::METHOD_SOURCE_WAIT_FOR_SYNCHRONIZATION, events[3][:method_source]
  end

  def test_library_frame_classification
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    Excon.get('http://google.com')
    @subject.float_return
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}
    library_methodinfo = events[2]
    assert_equal 'Excon', library_methodinfo[:class_name]
    assert_equal 'get', library_methodinfo[:method_name]
    assert_equal Raygun::Apm::Tracer::METHOD_SOURCE_KNOWN_LIBRARY, library_methodinfo[:method_source]
  end

  def test_tracer_gc
    tracer = Raygun::Apm::Tracer.new
    assert tracer.start_trace
    assert tracer.end_trace
    tracer = nil
    GC.start
  end

  def test_tracer_enable_disable
    tracer = Raygun::Apm::Tracer.new
    refute tracer.end_trace
    assert tracer.start_trace
    refute tracer.start_trace
    assert tracer.end_trace
    refute tracer.end_trace
  end

  def test_third_party_library_nested_method_exceptions_not_observed
    require "erb"
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.add_whitelist("ERB", nil)

    expected_events = []

    tracer.start_trace

    template = "Today is <%= raise 'foo' rescue nil %>."
    renderer = ERB.new(template)
    5.times do
      renderer.result()
    end

    tracer.end_trace

    # Expect 0 because we're already several levels deep into ERB by the time the renderer's result is called
    assert_equal 0, events.select{|e| Raygun::Apm::Event::ExceptionThrown === e }.size
  end

  def test_third_party_library_nested_method_is_not_profiled
    require "erb"
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.debug_blacklist = true

    # Test that current module is profiled 1 level deep

    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.add_whitelist("ERB::", nil)

    # XXX spurious methodinfos are fine for now, revisit
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]

    tracer.start_trace

    compiler = ERB::Compiler.new('<>')
    compiler.pre_cmd    = ["_erbout=+''"]
    compiler.put_cmd    = "_erbout.<<"
    compiler.insert_cmd = "_erbout.<<"
    compiler.post_cmd   = ["_erbout"]
    code, enc = compiler.compile("Got <%= obj %>!\n")

    tracer.end_trace

    assert_equal expected_events, events.map{|e| e.class }
  end

  def test_deep_stack_none_recursive
    events = []
    tracer = Raygun::Apm::Tracer.new

    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
    ]

    256.times do
      expected_events << Raygun::Apm::Event::Methodinfo
      expected_events << Raygun::Apm::Event::Begin
    end

    256.times do
      expected_events << Raygun::Apm::Event::End
    end

    tracer.start_trace
    @subject.deep_stack_method1

    methodinfos = events.select{|e| Raygun::Apm::Event::Methodinfo === e }.map{|e| [e[:function_id], e[:method_name]] }
    begins = events.select{|e| Raygun::Apm::Event::Begin === e }.map{|e| e[:function_id] }
    ends = events.select{|e| Raygun::Apm::Event::End === e }.map{|e| e[:function_id] }

    assert_equal expected_events, events.map{|e| e.class }

    assert_equal 256, methodinfos.size
    assert_equal 256, begins.size
    assert_equal 256, ends.size

    assert_equal begins, ends.reverse
  ensure
    tracer.end_trace
  end

  def test_vm_profiler_stack_overflow_stops_following
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo
    ]

    256.times do
      expected_events << Raygun::Apm::Event::Begin
    end

    256.times do
      expected_events << Raygun::Apm::Event::End
    end

    tracer.start_trace
    @subject.recursive_limited(300)

    begins = events.select{|e| Raygun::Apm::Event::Begin === e }.map{|e| e[:function_id] }
    ends = events.select{|e| Raygun::Apm::Event::End === e }.map{|e| e[:function_id] }

    assert_equal expected_events, events.map{|e| e.class }

    assert_equal begins, ends.reverse
  ensure
    tracer.end_trace
  end

  def test_vm_stack_overflow
    ruby_vm_max_frames = 0
    begin
      @subject.recursive
    rescue SystemStackError
      ruby_vm_max_frames = @subject.stack_frames
    end

    p "Ruby stack max frames before SystemStackError: #{ruby_vm_max_frames}"

    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    assert_raises SystemStackError do
      @subject.recursive
    end

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin
    ]

    255.times do
      expected_events << Raygun::Apm::Event::Begin
    end

    assert_equal expected_events, events.map{|e| e.class }
  ensure
    tracer.end_trace
  end

  def test_debug_sink
    tracer = Raygun::Apm::Tracer.new
    tracer.start_trace
    Thread.new do
      begin
        @subject.exception_raised
      rescue StandardError
      end
    end.join
    tracer.end_trace
    tracer.process_ended
  end

  def test_blacklisted_internal_api
    tracer = Raygun::Apm::Tracer.new
    tracer.debug_blacklist = true

    assert tracer.add_blacklist(nil, "after_remove_for_")

    assert tracer.whitelisted?("Net::HTTP#get", "Net::HTTP", "get")
    assert tracer.whitelisted?("Net::HTTP#delete", "Net::HTTP", "delete")
    refute tracer.whitelisted?("Net::HTTP#foo", "Net::HTTP", "foo")
    assert tracer.whitelisted?("Net#foo", "Net", "foo")

    assert tracer.blacklisted?("Method#arity", "Method", "arity")
    assert tracer.whitelisted?("MethodController#index", "MethodController", "index")

    assert tracer.blacklisted?("TypeError#initialize", "TypeError", "initialize")

    assert tracer.whitelisted?("Redis#set", "Redis", "set")
    assert tracer.blacklisted?("Redis#other", "Redis", "other")
    assert tracer.whitelisted?("Redis#sync", "Redis", "sync")

    assert tracer.whitelisted?("Raygun#track_exception", "Raygun", "track_exception")
    assert tracer.blacklisted?("Raygun#track_exception_sync", "Raygun", "track_exception_sync")
    assert tracer.blacklisted?("Raygun#log", "Raygun", "log")
    assert tracer.blacklisted?("Raygun#foobar", "Raygun", "foobar")
    assert tracer.blacklisted?("Raygun::Configuration#read", "Raygun::Configuration", "read")

    assert tracer.blacklisted?("Post#after_remove_for_comments", "Post", "after_remove_for_comments")

    tracer.add_blacklist("ActiveStorage::Blob", nil)
    tracer.add_whitelist("ActiveStorage::Blob", "upload")

    assert tracer.blacklisted?("ActiveStorage::Blob#build_after_upload", "ActiveStorage::Blob", "build_after_upload")
    assert tracer.whitelisted?("ActiveStorage::Blob#upload", "ActiveStorage::Blob", "upload")

    tracer.add_blacklist("#<ActiveRecord", nil)
    assert tracer.blacklisted?("#<ActiveRecord::AttributeMethods::GeneratedAttributeMethods:0x000055859335c758>.__temp__f6074796f6e637", "#<ActiveRecord::AttributeMethods::GeneratedAttributeMethods:0x000055859335c758>", "__temp__f6074796f6e637")

    assert tracer.whitelisted?("QueueController#index", "QueueController", "index")
    assert tracer.whitelisted?("TimeController#index", "TimeController", "index")

    assert tracer.blacklisted?("IO#read", "IO", "read")
    assert tracer.blacklisted?("IO::Foo#read", "IO::Foo", "read")

  end

  def test_sink_setters
    tracer = Raygun::Apm::Tracer.new
    assert_fatal_error(/Expected a Proc callback as sink/) do
      tracer.callback_sink = :invalid
    end
    assert_fatal_error(/Expected a Proc callback as sink/) do
      tracer.callback_sink = @subject
    end
    assert_fatal_error(/Expected a sink callback that accepts exactly 1 argument/) do
      tracer.callback_sink = Proc.new {|a, b| }
    end

    assert_fatal_error(/Expected a UDP socket that responds to 'send'/) do
      tracer.udp_sink(socket: :invalid)
    end
    assert_fatal_error(/Expected the UDP socket hostname to be a string/) do
      tracer.udp_sink(socket: UDPSocket.new, host: :invalid)
    end
    assert_fatal_error(/Expected the UDP socket port to be a numerical value/) do
      tracer.udp_sink(socket: UDPSocket.new, host: 'localhost', port: :invalid)
    end
    assert_fatal_error(/Expected the UDP receive buffer size to be a numerical value/) do
      tracer.udp_sink(socket: UDPSocket.new, host: 'localhost', port: 100, receive_buffer_size: :invalid)
    end
  end

  def test_builtin_functions
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.debug_blacklist = true
    tracer.callback_sink = Proc.new do |event|
      events << event
    end
    tracer.start_trace
    sleep 1
    system("date")
    tracer.end_trace
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
   ]

    assert_equal expected_events, events.map{|e| e.class}
    assert_equal 'Object', events[2][:class_name]
    assert_equal 'sleep', events[2][:method_name]
    assert_equal 'Object', events[5][:class_name]
    assert_equal 'system', events[5][:method_name]
  end

  def test_tracer_test_method_nested; end
  def test_tracer_test_method; test_tracer_test_method_nested; end
  def test_tracer
    events = []
    exception_id = nil
    exception_correlation_id = nil
    tracer = Raygun::Apm::Tracer.new

    tracer.debug_blacklist = true
    tracer.callback_sink = Proc.new do |event|
      events << event
    end
    # XXX fragile? Give pending framework threads time to start before we start collecting info
    Thread.pass; Thread.pass; Thread.pass
    assert tracer.add_whitelist "Raygun::ApmTest", nil
    tracer.start_trace
    Thread.new do
      test_tracer_test_method
      begin
        @subject.exception_raised
      rescue StandardError => e
        # Object IDs are monotomically increasing since 2.7, no public API to map memory address to object anymore
        exception_id = tracer.memory_address(e)
        exception_correlation_id = e.instance_variable_get(:@__raygun_correlation_id)
      end
    end.join
    test_tracer_test_method
    tracer.end_trace
    tracer.process_ended
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::ExceptionThrown,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
      Raygun::Apm::Event::ProcessEnded
    ]

    assert_equal expected_events, events.map{|e| e.class}

    process_frequency = events[0]
    assert process_frequency.is_a? Raygun::Apm::Event::ProcessFrequency
    assert (Time.now.to_i - process_frequency[:timestamp]) < 10

    thread_started = events[2]

    assert_equal 3, thread_started[:tid]
    methodinfo = events[3]
    assert_equal 3, methodinfo[:tid]
    assert_equal 'Raygun::ApmTest', methodinfo[:class_name]
    assert_equal 'test_tracer_test_method', methodinfo[:method_name]
    assert_equal 1, methodinfo[:function_id]
    e_begin = events[4]
    assert_equal 1, e_begin[:function_id]
    assert_equal 3, e_begin[:tid]
    methodinfo = events[5]
    assert_equal 3, methodinfo[:tid]
    assert_equal 'Raygun::ApmTest', methodinfo[:class_name]
    assert_equal 'test_tracer_test_method_nested', methodinfo[:method_name]
    assert_equal 2, methodinfo[:function_id]

    e_begin = events[6]
    assert_equal 2, e_begin[:function_id]
    assert_equal 3, e_begin[:tid]
    e_end = events[7]
    assert_equal 2, e_end[:function_id]
    assert_equal 3, e_end[:tid]

    e_end = events[8]
    assert_equal 1, e_end[:function_id]
    assert_equal 3, e_end[:tid]

    methodinfo = events[9]
    assert_equal 3, methodinfo[:tid]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal 'exception_raised', methodinfo[:method_name]
    assert_equal 3, methodinfo[:function_id]
    e_begin = events[10]
    assert_equal 3, e_begin[:function_id]
    assert_equal 3, e_begin[:tid]

    exception = events[11]
    assert_equal exception_id, exception[:exception_id]
    assert_equal Process.pid.to_s, exception_correlation_id.split("-").first

    e_end = events[12]
    assert_equal 3, e_end[:function_id]
    assert_equal 3, e_end[:tid]

    thread_ended = events[13]
    assert_equal 3, thread_ended[:tid]

    e_begin = events[14]
    assert_equal 1, e_begin[:function_id]
    assert_equal 1, e_begin[:tid]

    e_begin = events[15]
    assert_equal 2, e_begin[:function_id]
    assert_equal 1, e_begin[:tid]
    e_end = events[16]
    assert_equal 2, e_end[:function_id]
    assert_equal 1, e_end[:tid]
    e_end = events[17]
    assert_equal 1, e_end[:function_id]
    assert_equal 1, e_end[:tid]

    instance_ids = events.select{|e| e.class == Raygun::Apm::Event::Begin}.map{|e| e[:instance_id]}.uniq
    assert_equal 2, instance_ids.size
  end

  def test_callback_sink_errors
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      raise "XXX"
    end
    tracer.start_trace
    @subject.blacklist1
    tracer.end_trace
    assert events.empty?
  end

  def test_no_exceptions_on_entrypoint_frame
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    @subject.block_method do
      begin
        raise "Error invisible on the entrypoint frame (function ID 1)"
      rescue
      end
    end
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]
    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 1, methodinfo[:function_id]
  end

  def test_blacklist_add_api
    tracer = Raygun::Apm::Tracer.new
    refute tracer.add_blacklist nil, nil
    assert tracer.add_blacklist "Test", nil
    assert tracer.add_blacklist nil, "test"
  end

  def test_noop_bang
    tracer = Raygun::Apm::Tracer.new
    tracer.noop!
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace

    # Extended events driven by external notification systems should be gated too
    event = Raygun::Apm::Event::HttpOut.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:url] = 'https://google.com/' # 1300 68747470733A2F2F676F6F676C652E636F6D2F
    event[:verb] = 'GET' # 03 474554
    event[:status] = 200 # C800
    event[:duration] = 1000
    refute tracer.emit(event)

    assert events.empty?
  end

  def test_live_blacklist_file
    ENV['PROTON_USER_OVERRIDES_FILE'] = File.join(__dir__, 'blacklist_live.txt')
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
    ]

    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist2", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]
  ensure
    ENV.delete('PROTON_USER_OVERRIDES_FILE')
  end

  def test_blacklist_overrides_file
    ENV['PROTON_USER_OVERRIDES_FILE'] = File.join(__dir__, 'blacklist_overrides.txt')
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
    ]

    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist2", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]
  ensure
    ENV.delete('PROTON_USER_OVERRIDES_FILE')
  end

  def test_blacklist_overrides_file_overrides_defaults
    ENV['PROTON_USER_OVERRIDES_FILE'] = File.join(__dir__, 'blacklist_overrides_defaults.txt')
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    pp []
    tracer.end_trace

  if RUBY_VERSION >= "3.1"
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
    ]
  else
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
    ]
  end

    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'PP', methodinfo[:class_name]
  if RUBY_VERSION >= "3.1"
    assert_equal "width_for", methodinfo[:method_name]
  else
    assert_equal "pp", methodinfo[:method_name]
  end
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]
  ensure
    ENV.delete('PROTON_USER_OVERRIDES_FILE')
  end

  def test_blacklist
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]
    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist1", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]
    methodinfo = events[5]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist2", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[6][:function_id]
    assert_equal methodinfo[:function_id], events[7][:function_id]


    events = []
    tracer.start_trace
    assert tracer.add_blacklist 'Subject', 'blacklist1'
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction,
    ]

    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist2", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]

    events = []
    tracer.start_trace
    assert tracer.add_blacklist 'Subject', nil
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::EndTransaction
    ]
    assert_equal expected_events, events.map{|e| e.class}

    events = []
    tracer.start_trace
    assert tracer.add_whitelist 'Subject', 'blacklist2'
    @subject.blacklist1
    @subject.blacklist2
    tracer.end_trace
    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::EndTransaction
    ]
    assert_equal expected_events, events.map{|e| e.class}
    methodinfo = events[2]
    assert_equal 'Subject', methodinfo[:class_name]
    assert_equal "blacklist2", methodinfo[:method_name]
    assert_equal methodinfo[:function_id], events[3][:function_id]
    assert_equal methodinfo[:function_id], events[4][:function_id]
  end

  def test_only_one_sink_allowed
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
    end

    assert_raises Raygun::Apm::FatalError do
      tracer.udp_sink!
    end

    tracer = Raygun::Apm::Tracer.new
    tracer.udp_sink!

    assert_raises Raygun::Apm::FatalError do
      tracer.callback_sink = Proc.new do |event|
      end
    end
  end

  def test_extend_blacklist
    Raygun::Apm::Blacklist.extend_with %w(foo)
    Raygun::Apm::Blacklist.extend_with %w(bar)
    assert Raygun::Apm::Blacklist.resolve_entries.include?("RangeError#")
    assert Raygun::Apm::Blacklist.resolve_entries.include?("foo")
    assert Raygun::Apm::Blacklist.resolve_entries.include?("bar")
  ensure
    Raygun::Apm::Blacklist.extended_blacklist.clear
  end

  def test_wildcard_blacklist
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    assert tracer.add_blacklist nil, 'default_scope_override'
    @subject.default_scope_override
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}

    events = []
    tracer.start_trace
    assert tracer.add_blacklist nil, 'before_add_for_'
    @subject.before_add_for_comments
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}
  end

  def test_instance_id_unique
    tracer = Raygun::Apm::Tracer.new
    events = []
    tracer.callback_sink = Proc.new do |event|
      events << event
    end
    tracer.start_trace
    100.times do
      @subject.boolean_return
    end
    tracer.end_trace
    instance_ids = events.select{|e| e.class == Raygun::Apm::Event::Begin}.map{|e| e[:instance_id] }.uniq
    assert_equal 1, instance_ids.size
  end

  def test_udp_sink
    tracer = Raygun::Apm::Tracer.new
    tracer.udp_sink!
    tracer.start_trace
    Thread.new do
      test_tracer_test_method
      begin
        @subject.exception_raised
      rescue StandardError
        @subject.boolean_return
        @subject.string_return
        @subject.largestring_return
        @subject.binary_string_return
        @subject.float_return
        @subject.symbol_return
        @subject.complex_method('a', 'b', 'c', 'd', 'e', d: 2, x: 1)
        @subject.catch_all('a', 'b', 'c', 'd', 'e', d: 2, x: 1)
      end
    end.join
    test_tracer_test_method
    tracer.end_trace
    tracer.process_ended

    thread_names = Thread.list.map(&:name)
    assert thread_names.include?("raygun udp sink")
    assert thread_names.include?("raygun timer")
    skip "assert udp output"
  end

  def test_invalidencoding_string_return
    tracer = Raygun::Apm::Tracer.new
    tracer.start_trace
    @subject.invalidencoding_string_return
    tracer.end_trace
    tracer.process_ended
  end

  def test_complex_method
    tracer = Raygun::Apm::Tracer.new
    @subject.complex_method('a', 'b', 'c', 'd', 'e', d: 2, x: 1)
    tracer.process_ended
  end

  def test_catch_all
    tracer = Raygun::Apm::Tracer.new
    tracer.start_trace
    @subject.catch_all('a', 'b', 'c', 'd', 'e', d: 2, x: 1)
    tracer.end_trace
    tracer.process_ended
  end

  def test_method_missing
    tracer = Raygun::Apm::Tracer.new
    tracer.start_trace
    @subject.non_existent_method
    tracer.end_trace
    tracer.process_ended
  end

  def test_aliased_methods
    tracer = Raygun::Apm::Tracer.new
    tracer.start_trace
    @subject.require("socket")
    tracer.end_trace
    tracer.process_ended
  end

  def test_extended_event_emission
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
      events << event
    end
    tracer.start_trace
    assert_fatal_error(/Invalid extended event - cannot decode/) do
      tracer.emit(:INVALID)
    end
    event = Raygun::Apm::Event::HttpOut.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:url] = 'https://google.com/' # 1300 68747470733A2F2F676F6F676C652E636F6D2F
    event[:verb] = 'GET' # 03 474554
    event[:status] = 200 # C800
    event[:duration] = 1000
    assert tracer.emit(event)
    tracer.end_trace
    tracer.process_ended
    assert events.any? { |e| e.class == Raygun::Apm::Event::HttpOut }
  end

  def test_duped_trace_contexts
    tracer = Raygun::Apm::Tracer.new
    10.times do
      tracer.start_trace
    end
    tracer.diagnostics
    10.times do
      Thread.new do
        tracer.start_trace
        sleep 2
      end
    end
    tracer.diagnostics
    tracer.end_trace
    tracer.diagnostics
  end

  describe 'config' do
    def test_proton_configs
      config = Raygun::Apm::Config.new({})
      assert_equal 'None', config.proton_debug_loglevel
      assert_nil config.proton_user_overrides_file
      assert_equal 'Udp', config.proton_network_mode
      assert_nil config.proton_file_ipc_folder
      assert 'False', config.proton_use_multicast
      assert_equal 500, config.proton_batch_idle_counter
    end

    def test_udp_configs
      config = Raygun::Apm::Config.new({})
      assert_equal '127.0.0.1', config.proton_udp_host
      assert_equal 2799, config.proton_udp_port
      config.env['PROTON_USE_MULTICAST'] = 'True'
      assert_equal '239.100.15.215', config.proton_udp_host
    end

    def test_udp_defaults
      config = Raygun::Apm::Config.new({})
      assert_equal '127.0.0.1', config.proton_udp_host
      assert_equal 2799, config.proton_udp_port
    end

    def test_udp_host_port_configs
      config = Raygun::Apm::Config.new({})
      config.env["PROTON_UDP_HOST"] = "8.8.8.8"
      config.env["PROTON_UDP_PORT"] = "6000"
      assert_equal "8.8.8.8", config.proton_udp_host
      assert_equal 6000, config.proton_udp_port
    end

    def test_tcp_defaults
      config = Raygun::Apm::Config.new({})
      assert_equal '127.0.0.1', config.proton_tcp_host
      assert_equal 2799, config.proton_tcp_port
    end

    def test_tcp_host_port_configs
      config = Raygun::Apm::Config.new({})
      config.env["PROTON_TCP_HOST"] = "8.8.8.8"
      config.env["PROTON_TCP_PORT"] = "6000"
      assert_equal "8.8.8.8", config.proton_tcp_host
      assert_equal 6000, config.proton_tcp_port
    end

    def test_loglevel
      config = Raygun::Apm::Config.new({})
      assert_equal Raygun::Apm::Tracer::LOG_NONE, config.loglevel
      config.env['PROTON_DEBUG_LOGLEVEL'] = 'Debug'
      assert_equal Raygun::Apm::Tracer::LOG_DEBUG, config.loglevel
    end

    def test_api_key
      config = Raygun::Apm::Config.new({})
      assert_equal "", config.proton_api_key
      config.env['PROTON_API_KEY'] = 'sekrit'
      assert_equal 'sekrit', config.proton_api_key
    end

    def test_environment
      config = Raygun::Apm::Config.new({})
      assert_equal Raygun::Apm::Tracer::ENV_PRODUCTION, config.environment
      config.env['RACK_ENV'] = 'development'
      assert_equal Raygun::Apm::Tracer::ENV_DEVELOPMENT, config.environment
      config.env['RACK_ENV'] = nil
      config.env['RAILS_ENV'] = 'development'
      assert_equal Raygun::Apm::Tracer::ENV_DEVELOPMENT, config.environment
      config.env['RAILS_ENV'] = nil
      config.env['RACK_ENV'] = "staging"
      assert_equal Raygun::Apm::Tracer::ENV_PRODUCTION, config.environment
    end

    def test_blacklist_overrides_path
      config = Raygun::Apm::Config.new({})
      # Without an API key set
      if Gem.win_platform?
        assert_equal config.blacklist_file, File.join(Raygun::Apm::Config::DEFAULT_BLACKLIST_PATH_WINDOWS, ".txt")
      else
        assert_equal config.blacklist_file, File.join(Raygun::Apm::Config::DEFAULT_BLACKLIST_PATH_UNIX, ".txt")
      end

      # With an API key set
      config.env['PROTON_API_KEY'] = 'sekrit'
      if Gem.win_platform?
        assert_equal config.blacklist_file, File.join(Raygun::Apm::Config::DEFAULT_BLACKLIST_PATH_WINDOWS, "sekrit.txt")
      else
        assert_equal config.blacklist_file, File.join(Raygun::Apm::Config::DEFAULT_BLACKLIST_PATH_UNIX, "sekrit.txt")
      end

      # Overrides file set
      config.env['PROTON_USER_OVERRIDES_FILE'] = '/foo/bar/sekrit.txt'
      assert_equal config.blacklist_file, '/foo/bar/sekrit.txt'
    end
  end
end
