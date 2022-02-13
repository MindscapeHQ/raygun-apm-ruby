require 'excon'

module Raygun
  module Apm
    module Hooks
      module Excon
        def request(params={}, &block)
          if tracer = Raygun::Apm::Tracer.instance
            started = tracer.now
            response = super
            ended = tracer.now
            event = raygun_apm_http_out_event
            event[:pid] = Process.pid
            event[:url] = "#{@data[:scheme]}://#{@data[:host]}/#{params[:path]}"
            event[:verb] = params[:method].to_s.upcase
            event[:status] = response.status
            event[:duration] = ended - started
            event[:timestamp] = started
            event[:tid] = tracer.get_thread_id(Thread.current)
            tracer.emit(event)
            response
          else
            super
          end
        end

        private
        def raygun_apm_http_out_event
          @_raygun_apm_http_out_event ||= Raygun::Apm::Event::HttpOut.new
        end
      end
    end
  end
end

Raygun::Apm::Tracer.patch(Excon::Connection, Raygun::Apm::Hooks::Excon)
