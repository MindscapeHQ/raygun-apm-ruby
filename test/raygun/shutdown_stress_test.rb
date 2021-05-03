require "test_helper"

class Raygun::ShutdownStressTest < Raygun::Test
  if ENV['STRESS_SHUTDOWN']  
    describe 'Shutdown stress with UDP sink' do
      def setup
        @subject = Subject.new
      end
      def test_tracer_stop_start_shutdown_stress
        pids = []
        50.times do |runs|
         pid = fork do
            tracer = Raygun::Apm::Tracer.new
            ObjectSpace.define_finalizer(@subject, proc { tracer.process_ended })
            tracer.udp_sink!
            iterations = rand(10000000000)
            iterated = 0
            tracer.start_trace
            begin
              Thread.new do
                iterations.times do
                  @subject.deep_stack_method1
                  iterated = iterated + 1
                end
              end.join
            rescue Interrupt => e
              p "Interrupted shutdown for PID #{Process.pid} iterations: #{iterations} iterated: #{iterated}"
              tracer.end_trace
              tracer.process_ended
            end
          end
          pids << [pid, rand(70.0)]
        end

        start = Time.now
        while pids.size != 0 do
          sleep 0.1
          pids.each do |info|
            pid, runtime = info
            if Time.now - start > runtime
              p "Kill PID #{pid} after #{runtime} seconds"
              Process.kill("INT", pid)
              pids.delete(info)
            end
          end
        end
      end
    end
  end
end
