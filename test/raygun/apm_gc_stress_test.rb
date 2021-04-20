require "test_helper"

class Raygun::ApmGCStressTest < Raygun::Test
  if ENV['STRESS_GC']
    describe 'GC stress with Proc / callback sink' do
      def setup
        @subject = Subject.new
      end
      def test_tracer_stop_start_gc_stress
        GC.stress = true
        20.times do
          tracer = Raygun::Apm::Tracer.new
          ObjectSpace.define_finalizer(@subject, proc { tracer.process_ended })
          tracer.callback_sink = Proc.new do |event|
          end
          tracer.start_trace
          Thread.new do
            @subject.boolean_return
          end.join
          tracer.end_trace
          tracer.process_ended
        end
      ensure
        GC.stress = false
      end

      def test_tracer_started_gc_stress
        GC.stress = true
        tracer = Raygun::Apm::Tracer.new
        ObjectSpace.define_finalizer(@subject, proc { tracer.process_ended })
        tracer.callback_sink = Proc.new do |event|
        end
        tracer.start_trace
        20.times do
          Thread.new do
            @subject.boolean_return
          end.join
        end
      ensure
        GC.stress = false
        tracer.end_trace
        tracer.process_ended
      end
    end
  
    describe 'GC stress with UDP sink' do
      def setup
        @subject = Subject.new
      end
      def test_tracer_stop_start_gc_stress
        GC.stress = true
        20.times do
          tracer = Raygun::Apm::Tracer.new
          ObjectSpace.define_finalizer(@subject, proc { tracer.process_ended })
          tracer.udp_sink!
          tracer.start_trace
          Thread.new do
            @subject.boolean_return
          end.join
          tracer.end_trace
          tracer.process_ended
        end
      ensure
        GC.stress = false
      end

      def test_tracer_started_gc_stress
        GC.stress = true
        tracer = Raygun::Apm::Tracer.new
        ObjectSpace.define_finalizer(@subject, proc { tracer.process_ended })
        tracer.udp_sink!
        tracer.start_trace
        20.times do
          Thread.new do
            @subject.boolean_return
          end.join
        end
      ensure
        GC.stress = false
        tracer.end_trace
        tracer.process_ended
      end
    end
  end
end