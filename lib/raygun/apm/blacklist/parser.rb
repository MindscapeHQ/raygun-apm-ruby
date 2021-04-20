module Raygun
  module Apm
    module Blacklist
      class Parser
        COMMENT = /^#[^<].*/
        ANONYMOUS = /^#<.*:.*>?/

        def initialize(tracer)
          @tracer = tracer
          @translator = Blacklist::Translator.new
        end

        def add_filters(filters)
          filters.each do |filter|
            filter.strip!
            add_filter(filter)
          end
          show_filters
        end

        private
        def add_filter(filter)
          if filter =~ COMMENT && filter !~ ANONYMOUS
            return 
          end
          if filter.start_with?('+')
            @tracer.add_whitelist *translate(filter[1..-1])
          elsif filter.start_with?('-')
            @tracer.add_blacklist *translate(filter[1..-1])
          elsif filter.size > 0
            @tracer.add_blacklist *translate(filter)
          end
        rescue => e
          puts "Failed to add line '#{filter}' to the blacklist (#{e}) #{e.backtrace.join("\n")}"
        end

        def show_filters
          @tracer.show_filters if @tracer.config.loglevel == Tracer::LOG_BLACKLIST
        end

        def translate(filter)
          @translator.translate(filter)
        end
      end
    end
  end
end
