require 'raygun/apm/blacklist'
require 'rbconfig'

module Raygun
  module Apm
    class Tracer
      @__pids ||= {}
      class << self
        def instance
          @__pids[Process.pid]
        end

        def instance=(tracer)
          @__pids[Process.pid] = tracer
        end
      end

      attr_accessor :config

      def initialize(env=ENV)
        configure(env)
        initialize_blacklist
        register_known_library_paths
        run_agent_connectivity_diagnostics
        ObjectSpace.define_finalizer(self, proc{ disable_tracepoints })
      # Any fails here is kamikaze for the tracer
      rescue => e
        # XXX works for the middleware wrapped case, not for standalone - revisit
        raise Raygun::Apm::FatalError, "Raygun APM tracer could not be initialized: #{e.message} #{e.backtrace.join("\n")}"
      end

      def udp_sink!
        require "socket"
        sock = UDPSocket.new
        # For UDP sockets, SO_SNDBUF is the max packet size and NOT send buffer as with a connection oriented transport
        sock.setsockopt(Socket::SOL_SOCKET, Socket::SO_SNDBUF, Tracer::BATCH_PACKET_SIZE)
        self.udp_sink(
          socket: sock,
          host: config.proton_udp_host,
          port: config.proton_udp_port,
          receive_buffer_size: sock.getsockopt(Socket::SOL_SOCKET, Socket::SO_RCVBUF).int
        )
      # Any fails here is kamikaze for the tracer
      rescue => e
        # XXX works for the middleware wrapped case, not for standalone - revisit
        raise Raygun::Apm::FatalError, "Raygun APM UDP sink could not be initialized: #{e.message} #{e.backtrace.join("\n")}"
      end

      private
      def configure(env)
        @config = Config.new(env)
        # Special assignments from config to the Tracer
        self.log_level = config.loglevel
        self.environment = config.environment
        self.api_key = config.proton_api_key
      end

      def initialize_blacklist
        @blacklist_parser = Raygun::Apm::Blacklist::Parser.new(self)
        file = @config.blacklist_file
        @blacklist = if file && File.exist?(file)
          File.readlines(file)
        else
          []
        end
        # Defaults
        @blacklist_parser.add_filters Raygun::Apm::Blacklist.resolve_entries
        # From file
        @blacklist_parser.add_filters @blacklist
      end

      def register_known_library_paths
        if defined?(Bundler)
          libs = Bundler.load.specs.map(&:full_gem_path).sort << RbConfig::CONFIG['rubylibdir']
          libs.delete(Dir.getwd)
          self.register_libraries libs
        else
          self.register_libraries [RbConfig::CONFIG['rubylibdir']]
        end
      end

      def run_agent_connectivity_diagnostics
        check = Raygun::Apm::Diagnostics.new
        check.verify_agent(self)
      end
    end
  end
end
