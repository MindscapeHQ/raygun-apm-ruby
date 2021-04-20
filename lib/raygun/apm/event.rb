module Raygun
  module Apm
    class Event
      def inspect
        "#<#{self.class.name}:#{self.object_id}> length:#{self.length} pid:#{self[:pid]} tid:#{self[:tid]} timestamp:#{self[:timestamp]}"
      end
      class ExceptionThrown < Event
        def inspect
          super + " class:#{self[:class_name]}"
        end
      end
      class ThreadStarted < Event
        def inspect
          super + " parent_tid:#{self[:parent_tid]}"
        end
      end
      class Begin < Event
        def inspect
          super + " function_id:#{self[:function_id]} instance_id:#{self[:instance_id]}"
        end
      end
      class End < Event
        def inspect
          super + " function_id:#{self[:function_id]}"
        end
      end
      class Methodinfo < Event
        def inspect
          super + " function_id:#{self[:function_id]} class_name:#{self[:class_name]} method_name:#{self[:method_name]} method_source:#{self[:method_source]}"
        end
      end
      class HttpOut < Event
        def inspect
          super + " url:#{self[:url]} verb:#{self[:verb]} status:#{self[:status]} duration:#{self[:duration]}"
        end
      end
      class Sql < Event
        def inspect
          super + " provider:#{self[:provider]} host:#{self[:host]} query:#{self[:query]} database:#{self[:database]} duration:#{self[:duration]}"
        end
      end
      class BeginTransaction < Event
        def inspect
          super + " api_key:#{self[:api_key]} technology_type:#{self[:technology_type]} process_type:#{self[:process_type]}"
        end
      end
    end
  end
end