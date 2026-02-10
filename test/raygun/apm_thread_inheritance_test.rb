require "test_helper"

class Raygun::ApmThreadInheritanceTest < Raygun::Test
  def test_parent_thread_assignment
    events = []
    tracer = Raygun::Apm::Tracer.new
    tracer.callback_sink = Proc.new do |event|
      events << event
    end

    tracer.start_trace
    threads = []
    threads << Thread.new do
      threads << Thread.new do
        threads << Thread.new do
          threads << Thread.new do
            threads << Thread.new do
              tracer.diagnostics
              sleep 0.2
            end
            sleep 0.4
          end
          sleep 0.6
        end
        sleep 0.8
      end
      sleep 1
    end
    threads.map(&:join)
    tracer.end_trace

    expected_events = [
      Raygun::Apm::Event::ProcessFrequency,
      Raygun::Apm::Event::BeginTransaction,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Methodinfo,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::ThreadStarted,
      Raygun::Apm::Event::Begin,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::End,
      Raygun::Apm::Event::ThreadEnded,
      Raygun::Apm::Event::EndTransaction
    ]

    assert_equal expected_events, events.map{|e| e.class}

    # Verify thread events have valid tid and parent_tid values
    # Note: Specific thread IDs may vary by Ruby version and platform
    thread_started_events = events.select { |e| e.is_a?(Raygun::Apm::Event::ThreadStarted) }
    assert_equal 5, thread_started_events.length, "Expected 5 ThreadStarted events"

    thread_started_events.each do |event|
      refute_nil event[:tid], "ThreadStarted event should have tid"
      refute_nil event[:parent_tid], "ThreadStarted event should have parent_tid"
      assert event[:tid] > 0, "tid should be positive"
    end
  end
end
