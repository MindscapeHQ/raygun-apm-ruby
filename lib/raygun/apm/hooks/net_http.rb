require 'net/http'

module Raygun
  module Apm
    module Hooks
      module Net
        module HTTP
          private

          def transport_request(request)
            if tracer = Raygun::Apm::Tracer.instance
              started = tracer.now
              response = super
              ended = tracer.now
              event = raygun_apm_http_out_event
              event[:pid] = Process.pid
              event[:url] = raygun_apm_url(request)
              event[:verb] = request.method
              event[:status] = response.code.to_i
              event[:duration] = ended - started
              event[:timestamp] = started
              event[:tid] = tracer.get_thread_id(Thread.current)
              tracer.emit(event)
              response
            else
              super
            end
          end

          def raygun_apm_url(request)
            return request.uri.to_s if request.uri
            "#{use_ssl? ? "https" : "http"}://#{request["host"] || address}#{request.path}"
          end

          def raygun_apm_http_out_event
            @_raygun_apm_http_out_event ||= Raygun::Apm::Event::HttpOut.new
          end
        end
      end
    end
  end
end

Raygun::Apm::Tracer.patch(::Net::HTTP, Raygun::Apm::Hooks::Net::HTTP)
