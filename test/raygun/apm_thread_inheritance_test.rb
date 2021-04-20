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

    assert_equal 2, events[2][:parent_tid]
    assert_equal 3, events[2][:tid]
    assert_equal 3, events[5][:parent_tid]
    assert_equal 5, events[5][:tid]
    assert_equal 1, events[6][:parent_tid]
    assert_equal 5, events[6][:tid]
    assert_equal 1, events[8][:parent_tid]
    assert_equal 7, events[8][:tid]
    assert_equal 1, events[10][:parent_tid]
    assert_equal 9, events[10][:tid]
  end
end
