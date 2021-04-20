require 'httpclient'

module Raygun
  module Apm
    module Hooks
      module HTTPClient
        private

        def do_request(method, uri, query, body, header, &filtered_block)
          if tracer = Raygun::Apm::Tracer.instance
            started = tracer.now
            response = super
            ended = tracer.now
            event = raygun_apm_http_out_event
            event[:pid] = Process.pid
            event[:url] = raygun_apm_url(uri, query)
            event[:verb] = method.to_s.upcase
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

        def raygun_apm_url(uri, query)
          uri = uri.to_param if uri.respond_to?(:to_param)
          query = query.to_param if query.respond_to?(:to_param)
          query && uri ? URI.join(uri, query).to_s : uri.to_s
        end

        def raygun_apm_http_out_event
          @_raygun_apm_http_out_event ||= Raygun::Apm::Event::HttpOut.new
        end
      end
    end
  end
end

HTTPClient.prepend(Raygun::Apm::Hooks::HTTPClient)
