require 'mongo'

module Raygun
  module Apm
    module Hooks
      module MongoDB
        def do_execute(connection, client, options = {})
          result = nil
          if tracer = Raygun::Apm::Tracer.instance
            started = tracer.now
            result = super
            ended = tracer.now
            event = raygun_apm_sql_event
            event[:pid] = Process.pid
            event[:query] = raygun_format_query(connection)
            event[:provider] = "mongodb"
            event[:host] = connection.address.to_s
            event[:database] = client.database.name
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
        def raygun_format_query(connection)
          payload = message(connection).payload
          return "#{payload["database_name"]}.#{payload["command_name"]} #{payload["command"]}}"
        end

        def raygun_apm_sql_event
          @_raygun_apm_sql_event ||= Raygun::Apm::Event::Sql.new
        end
      end
    end
  end
end

Mongo::Operation.constants.each do |operation|
  Raygun::Apm::Tracer.patch(Mongo::Operation.const_get(operation), Raygun::Apm::Hooks::MongoDB) rescue nil
end
