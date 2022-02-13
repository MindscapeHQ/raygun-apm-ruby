module Raygun
  module Apm
    class Config
      LOGLEVELS = {
        "None" => Tracer::LOG_NONE,
        "Info" => Tracer::LOG_INFO,
        "Warning" => Tracer::LOG_WARNING,
        "Error" => Tracer::LOG_ERROR,
        "Verbose" => Tracer::LOG_VERBOSE,
        "Debug" => Tracer::LOG_DEBUG,
        "Everything" => Tracer::LOG_EVERYTHING,
        # ruby profiler specific
        "Blacklist" => Tracer::LOG_BLACKLIST
      }

      ENVIRONMENTS = {
        "development" => Tracer::ENV_DEVELOPMENT,
        "production" => Tracer::ENV_PRODUCTION
      }

      DEFAULT_BLACKLIST_PATH_UNIX = "/usr/share/Raygun/Blacklist"
      DEFAULT_BLACKLIST_PATH_WINDOWS = "C:\\ProgramData\\Raygun\\Blacklist"

      attr_accessor :env
      def initialize(env=ENV)
        @env = env
      end

      def self.cast_to_boolean(x)
        case x
        when true, 'true', 'True', 1, '1' then true
        else
          false
        end
      end

      def self.config_var(attr, opts={}, &blk)
        define_method attr.downcase do
          val = if x = env[attr]
            if opts[:as] == Integer
              Integer(x)
            elsif opts[:as] == String
              x.to_s
            elsif opts[:as] == :boolean
              self.class.cast_to_boolean(x)
            end
          else
            opts[:default]
          end
          blk ? blk.call(val) : val
        end
      end

      # Initial constants for ProtonAgentTail.exe
      UDP_SINK_HOST = TCP_SINK_HOST = TCP_MANAGEMENT_HOST = '127.0.0.1'
      UDP_SINK_MULTICAST_HOST = '239.100.15.215'
      UDP_SINK_PORT = TCP_SINK_PORT = 2799
      TCP_MANAGEMENT_PORT = 2790

      ## Enumerate all PROTON_ constants
      config_var 'PROTON_API_KEY', as: String, default: ''
      config_var 'PROTON_DEBUG_LOGLEVEL', as: String, default: 'None'
      config_var 'PROTON_USER_OVERRIDES_FILE', as: String
      config_var 'PROTON_NETWORK_MODE', as: String, default: 'Udp'
      config_var 'PROTON_FILE_IPC_FOLDER', as: String
      config_var 'PROTON_USE_MULTICAST', as: String, default: 'False'
      config_var 'PROTON_BATCH_IDLE_COUNTER', as: Integer, default: 500
      ## New - Ruby profiler
      config_var 'PROTON_UDP_HOST', as: String, default: UDP_SINK_HOST
      config_var 'PROTON_UDP_PORT', as: Integer, default: UDP_SINK_PORT
      config_var 'PROTON_TCP_HOST', as: String, default: TCP_SINK_HOST
      config_var 'PROTON_TCP_PORT', as: Integer, default: TCP_SINK_PORT
      ## Conditional hooks
      config_var 'PROTON_HOOK_REDIS', as: :boolean, default: 'True'
      config_var 'PROTON_HOOK_INTERNALS', as: :boolean, default: 'True'

      def proton_udp_host
        if proton_use_multicast == 'True'
          UDP_SINK_MULTICAST_HOST
        else
          env['PROTON_UDP_HOST'] ? env['PROTON_UDP_HOST'].to_s : UDP_SINK_HOST
        end
      end

      def proton_tcp_host
        env['PROTON_TCP_HOST'] ? env['PROTON_TCP_HOST'].to_s : TCP_SINK_HOST
      end

      def loglevel
        LOGLEVELS[proton_debug_loglevel] || raise(ArgumentError, "invalid log level")
      end

      def environment
        environment = env['RACK_ENV'] || env['RAILS_ENV'] || 'production'
        ENVIRONMENTS[environment] || Tracer::ENV_PRODUCTION
      end

      # Prefer what is set by PROTON_USER_OVERRIDES_FILE env
      def blacklist_file
        return proton_user_overrides_file if proton_user_overrides_file
        path = Gem.win_platform? ? DEFAULT_BLACKLIST_PATH_WINDOWS : DEFAULT_BLACKLIST_PATH_UNIX
        "#{File.join(path, proton_api_key)}.txt"
      end
    end
  end
end
