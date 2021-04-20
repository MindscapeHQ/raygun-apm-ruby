require "socket"
require "json"

module Raygun
  module Apm
    class Diagnostics
      AGENT_STATE_DOWN = "\nThe Raygun APM Agent appears to not be running on the current host.\nIf not already installed, please consult https://raygun.com/documentation/product-guides/apm/agent/downloads/\nOtherwise refer to https://raygun.com/documentation/product-guides/apm/agent/installation/ for starting the Agent."
      AGENT_STATE_UNKNOWN = "\nThe Raygun APM Agent is reachable, but Unable to determine the state of the Agent at the moment."
      AGENT_STATE_UP_MISCONFIGURED = "\nThe Raygun APM Agent is running, but misconfigured.\nThe API Key needs to be set through the Raygun_ApiKey environment variable.\nThe API key can be found under 'Application Settings' in the Raygun UI"
      AGENT_STATE_UP_CONFIGURED = "\nThe Raygun APM Agent is configured properly!"
      AGENT_MINIMUM_VERSION_NOT_MET = "\nVersion #{Raygun::Apm::VERSION} of the Raygun APM Profiler requires a minimum Agent version #{Raygun::Apm::MINIMUM_AGENT_VERSION}\nPlease download the latest Agent from https://raygun.com/documentation/product-guides/apm/agent/downloads/"
      PROFILER_NOOPED = "Profiler loaded in noop mode and will be disabled due to the minimum Agent version not met"

      def initialize(host: Apm::Config::TCP_MANAGEMENT_HOST, port: Apm::Config::TCP_MANAGEMENT_PORT)
        @host = host
        @port = port
      end

      def verify_agent(tracer)
        socket.write "GetAgentInfo"
        response = JSON.parse(socket.gets)
        if minimum_agent_version_not_met?(response['Version'])
          puts AGENT_MINIMUM_VERSION_NOT_MET
          tracer.noop!
          puts PROFILER_NOOPED
        else
          if response['Status'] == 1
            puts AGENT_STATE_UP_CONFIGURED
          elsif response['Status'] == 0
            puts AGENT_STATE_UP_MISCONFIGURED
          end
        end
      rescue Errno::ECONNREFUSED
        puts AGENT_STATE_DOWN
      rescue
        puts AGENT_STATE_UNKNOWN
      end

      private
      def socket
        @socket ||= s = TCPSocket.new(@host, @port)
      end

      def minimum_agent_version_not_met?(version)
        # Legacy path
        if String === version
          version < Raygun::Apm::MINIMUM_AGENT_VERSION
        else
          "#{version['Major']}.#{version['Minor']}.#{version['Build']}.#{version['Revision']}" < Raygun::Apm::MINIMUM_AGENT_VERSION
        end
      end
    end
  end
end
