require "test_helper"

class Raygun::ApmThreadSafetyTest < Raygun::Test

  if ENV['STRESS_THREADS']
    describe 'context' do
      def setup
        @subject = Subject.new
      end
      def test_buffer_thread_safety_check_by_event_size_callback_sink
        threads, events =  [], {}
        tracer = Raygun::Apm::Tracer.new
        tracer.callback_sink = Proc.new do |event|
          tid, _ = event[:tid], event.encoded
          # skip main thread events, process started etc
          # also skip test runner thread events (block evocations)
          next if tid == 0 || tid == 1
          # skip methodinfo events since they will be interleaved across threads and we're not sure how to test them just yet
          next if event.class == Raygun::Apm::Event::Methodinfo
          events[tid] ||= []
          events[tid] << event
        end
        # XXX fragile? Give pending framework threads time to start before we start collecting info
        Thread.pass; Thread.pass; Thread.pass
        tracer.start_trace
        times = 1000
        times.times do
          threads << Thread.new do
            sleep(rand(0.05..0.1))
            begin
              @subject.block_method do
                @subject.exception_raised
              end
            rescue
            end
          end
        end
        threads.each(&:join)
        tracer.end_trace
        tracer.process_ended
        assert_equal times, events.keys.size

        expected_types = [
          Raygun::Apm::Event::ThreadStarted,
          Raygun::Apm::Event::Begin,
          Raygun::Apm::Event::End,
          Raygun::Apm::Event::Begin,
          Raygun::Apm::Event::Begin,
          Raygun::Apm::Event::ExceptionThrown,
          Raygun::Apm::Event::End,
          Raygun::Apm::Event::End,
          Raygun::Apm::Event::ThreadEnded
        ]

        events.each do |tid, thread_events|
          types = thread_events.map{|e| e.class}
          assert_equal expected_types, types

          assert_equal 23, thread_events[0].encoded.bytes.size
          assert_equal 32, thread_events[1].encoded.bytes.size
          assert_equal 28, thread_events[2].encoded.bytes.size
          assert_equal 32, thread_events[3].encoded.bytes.size
          assert_equal 32, thread_events[4].encoded.bytes.size
          assert thread_events[5].encoded.bytes.size >= 78
          assert_equal 28, thread_events[6].encoded.bytes.size
          assert_equal 28, thread_events[7].encoded.bytes.size
          assert_equal 19, thread_events[8].encoded.bytes.size
        end
      end

      # Additional thread safety check for batched sinks (UDP and upcoming File sink)
      def test_buffer_thread_safety_check_by_event_size_batched_sink
        threads =  []
        tracer = Raygun::Apm::Tracer.new
        tracer.udp_sink!
        # XXX fragile? Give pending framework threads time to start before we start collecting info
        Thread.pass; Thread.pass; Thread.pass
        tracer.start_trace
        times = 1000
        times.times do
          threads << Thread.new do
            sleep(rand(0.05..0.1))
            begin
              @subject.block_method do
                @subject.exception_raised
              end
            rescue
            end
          end
        end
        threads.each(&:join)
        tracer.end_trace
        tracer.process_ended
      end
    end
  end
end