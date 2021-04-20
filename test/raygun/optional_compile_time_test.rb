require "test_helper"

# For compile time concerns such as RB_RG_EMIT_ARGUMENTS and RB_RG_TRACE_BLOCKS
class OptionalCompileTimeTest < Raygun::Test

  if Raygun::Apm::Tracer::FEATURE_TRACE_BLOCKS
    def test_tracer_block_call
      proc = Proc.new do |x|
        x
      end
      tracer = Raygun::Apm::Tracer.new
      events = []
      tracer.callback_sink = Proc.new do |event|
        events << event
      end
      tracer.start_trace
      proc.call 'proc1'
      proc.call 'proc2'
      tracer.end_trace
      tracer.process_ended
      expected_events = [
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::ProcessStarted,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::ProcessEnded
      ]
      assert_equal expected_events, events.map{|e| e.class}
      methodinfo = events[10]
      assert_equal 'Raygun::ApmTest', methodinfo[:class_name]
      assert_equal "block:test_tracer_block_call:12", methodinfo[:method_name]

      begin1 = events[12]
      assert_equal 2, begin1[:function_id]
      end1 = events[13]
      assert_equal 2, end1[:function_id]

      begin2 = events[13]
      assert_equal 2, begin2[:function_id]
      end2 = events[14]
      assert_equal 2, end2[:function_id]

      assert begin1[:instance_id] != begin2[:instance_id]
    end

    def test_tracer
      events = []
      exception_id = nil
      tracer = Raygun::Apm::Tracer.new
      tracer.callback_sink = Proc.new do |event|
        events << event
      end
      # XXX fragile? Give pending framework threads time to start before we start collecting info
      Thread.pass; Thread.pass; Thread.pass
      tracer.start_trace
      Thread.new do
        test_tracer_test_method
        begin
          @subject.exception_raised
        rescue StandardError => e
          # Object IDs are monotomically increasing since 2.7, no public API to map memory address to object anymore
          exception_id = tracer.memory_address(e)
        end
      end.join
      test_tracer_test_method
      tracer.end_trace
      tracer.process_ended

      expected_events = [
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::ProcessStarted,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::ThreadStarted,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
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
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::ThreadEnded,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::ProcessEnded
      ]
      assert_equal expected_events, events.map{|e| e.class}

      process_frequency = events[0]
      assert process_frequency.is_a? Raygun::Apm::Event::ProcessFrequency
      # test that event timestamp is relatively close to Time.now in seconds
      assert (Time.now.to_i - process_frequency[:timestamp] / 1000000) < 10

      thread_started = events[10]

      thread_block_methodinfo = events[11]
      assert_equal 3, thread_block_methodinfo[:tid]
      assert_equal 2, thread_block_methodinfo[:function_id]
      thread_block_start = events[12]
      assert_equal 3, thread_block_start[:tid]
      assert_equal 2, thread_block_start[:function_id]

      assert_equal 3, thread_started[:tid]
      methodinfo = events[13]
      assert_equal 3, methodinfo[:tid]
      assert_equal 'Raygun::ApmTest', methodinfo[:class_name]
      assert_equal 'test_tracer_test_method', methodinfo[:method_name]
      assert_equal 3, methodinfo[:function_id]
      e_begin = events[14]
      assert_equal 3, e_begin[:function_id]
      assert_equal 3, e_begin[:tid]
      methodinfo = events[15]
      assert_equal 3, methodinfo[:tid]
      assert_equal 'Raygun::ApmTest', methodinfo[:class_name]
      assert_equal 'test_tracer_test_method_nested', methodinfo[:method_name]
      assert_equal 4, methodinfo[:function_id]

      e_begin = events[16]
      assert_equal 4, e_begin[:function_id]
      assert_equal 3, e_begin[:tid]
      e_end = events[17]
      assert_equal 4, e_end[:function_id]
      assert_equal 3, e_end[:tid]

      e_end = events[18]
      assert_equal 3, e_end[:function_id]
      assert_equal 3, e_end[:tid]

      methodinfo = events[19]
      assert_equal 3, methodinfo[:tid]
      assert_equal 'Subject', methodinfo[:class_name]
      assert_equal 'exception_raised', methodinfo[:method_name]
      assert_equal 5, methodinfo[:function_id]
      e_begin = events[20]
      assert_equal 5, e_begin[:function_id]
      assert_equal 3, e_begin[:tid]

      exception = events[21]
      assert_equal exception_id, exception[:exception_id]

      e_end = events[22]
      assert_equal 5, e_end[:function_id]
      assert_equal 3, e_end[:tid]

      thread_block_end = events[23]
      assert_equal 2, thread_block_end[:function_id]
      assert_equal 3, thread_block_end[:tid]

      thread_ended = events[24]
      assert_equal 3, thread_ended[:tid]

      e_begin = events[25]
      assert_equal 3, e_begin[:function_id]
      assert_equal 1, e_begin[:tid]

      e_begin = events[26]
      assert_equal 4, e_begin[:function_id]
      assert_equal 1, e_begin[:tid]
      e_end = events[27]
      assert_equal 4, e_end[:function_id]
      assert_equal 1, e_end[:tid]
      e_end = events[28]
      assert_equal 3, e_end[:function_id]
      assert_equal 1, e_end[:tid]

      instance_ids = events.select{|e| e.class == Raygun::Apm::Event::Begin}.map{|e| e[:instance_id]}.uniq
      assert_equal 1, instance_ids.size
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
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End
      ]
      assert_equal expected_events, events.map{|e| e.class}
      methodinfo = events[7]
      assert_equal 'Subject', methodinfo[:class_name]
      assert_equal "blacklist1", methodinfo[:method_name]
      assert_equal methodinfo[:function_id], events[8][:function_id]
      assert_equal methodinfo[:function_id], events[9][:function_id]
      methodinfo = events[10]
      assert_equal 'Subject', methodinfo[:class_name]
      assert_equal "blacklist2", methodinfo[:method_name]
      assert_equal methodinfo[:function_id], events[11][:function_id]
      assert_equal methodinfo[:function_id], events[12][:function_id]
    
      events = []
      tracer.start_trace
      tracer.add_blacklist 'Subject::blacklist1'
      @subject.blacklist1
      @subject.blacklist2
      tracer.end_trace
      expected_events = [
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End
      ]
      assert_equal expected_events, events.map{|e| e.class}
      methodinfo = events[7]
      assert_equal 'Subject', methodinfo[:class_name]
      assert_equal "blacklist2", methodinfo[:method_name]
      assert_equal methodinfo[:function_id], events[8][:function_id]
      assert_equal methodinfo[:function_id], events[9][:function_id]
    
      events = []
      tracer.start_trace
      tracer.add_blacklist 'Subject'
      @subject.blacklist1
      @subject.blacklist2
      tracer.end_trace
      expected_events = [
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End
      ]
      assert_equal expected_events, events.map{|e| e.class}
    
      events = []
      tracer.start_trace
      tracer.add_whitelist 'Subject::blacklist2'
      @subject.blacklist1
      @subject.blacklist2
      tracer.end_trace
      expected_events = [
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::End,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End,
      ]
      assert_equal expected_events, events.map{|e| e.class}
      methodinfo = events[7]
      assert_equal 'Subject', methodinfo[:class_name]
      assert_equal "blacklist2", methodinfo[:method_name]
      assert_equal methodinfo[:function_id], events[8][:function_id]
      assert_equal methodinfo[:function_id], events[9][:function_id]
    end
  end

  if Raygun::Apm::Tracer::FEATURE_EMIT_ARGUMENTS
    # Test string sizes from normal string to largestring,
    # accounting for encoding overhead.
    def test_string_overflow_to_largestring
      tracer = Raygun::Apm::Tracer.new
      events = []
      tracer.callback_sink = Proc.new do |event|
        events << event
      end
      tracer.start_trace
      str = '1'*1000
      @subject.string_return(str)
      @subject.string_return(str*2)
      @subject.string_return(str*3)
      @subject.string_return(str*4)
      @subject.string_return(str*5)
      tracer.end_trace
      assert_equal 13, events.size
      skip 'test codepoint boundary overflow'
      skip 'test result event types'
    end
      
    def test_bignum_overflow_to_string_on_larger_than_platform_long_long
      tracer = Raygun::Apm::Tracer.new
      events = []
      tracer.callback_sink = Proc.new do |event|
        events << event
      end
      tracer.start_trace
      @subject.bignum_return
      tracer.end_trace
      assert_equal 5, events.size
      expected_events = [
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End
      ]
      assert_equal expected_events, events.map{|e| e.class}
      e_end = events[4]
      assert_equal 4139, e_end.length
      skip "assert e_end[:returnvalue] is (1 << (1 << 16))[0..bigstring_max]"
    end
      
    def test_truncated_variable_name
      tracer = Raygun::Apm::Tracer.new
      events = []
      tracer.callback_sink = Proc.new do |event|
        events << event
      end
      tracer.start_trace
      @subject.truncated_variable_name
      tracer.end_trace
      
      assert_equal 5, events.size
      expected_events = [
        Raygun::Apm::Event::ProcessFrequency,
        Raygun::Apm::Event::ProcessType,
        Raygun::Apm::Event::Methodinfo,
        Raygun::Apm::Event::Begin,
        Raygun::Apm::Event::End
      ]
      assert_equal expected_events, events.map{|e| e.class}
      e_begin = events[3]
      e_end = events[4]
      assert_equal 1856, e_begin.length
      assert_equal 39, e_end.length
    end
  end    
end
