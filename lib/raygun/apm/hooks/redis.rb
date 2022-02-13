module Raygun
  module Apm
    module Hooks
      module Redis
        def process(commands)
          result = nil
          if tracer = Raygun::Apm::Tracer.instance
            started = tracer.now
            result = super
            ended = tracer.now
            event = raygun_apm_sql_event
            event[:pid] = Process.pid
            event[:query] = raygun_format_query(commands)
            event[:provider] = "redis"
            event[:host] = "#{host}:#{port}"
            event[:database] = db.to_s
            event[:duration] = ended - started
            event[:timestamp] = started
            event[:tid] = tracer.get_thread_id(Thread.current)
            tracer.emit(event)
            result
          else
            super
          end
        end

        private
        def raygun_format_query(commands)
          commands.map do |command|
            command.map(&:to_s).join(" ")
          end.join(", ")
        end

        def raygun_apm_sql_event
          @_raygun_apm_sql_event ||= Raygun::Apm::Event::Sql.new
        end

        def raygun_redis_call
          yield
        end
      end
    end
  end
end

Raygun::Apm::Tracer.patch(::Redis::Client, Raygun::Apm::Hooks::Redis)
