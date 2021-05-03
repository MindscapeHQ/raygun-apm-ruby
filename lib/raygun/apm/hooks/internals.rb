require 'thread'

module Raygun
  module Apm
    module Hooks
      module Object
        def system(*args)
          super
        end

        def sleep(*args)
          super
        end

        def exec(*args)
          super
        end

        def spawn(*args)
          super
        end

        def fork(*args)
          super
        end
      end

      module IO
        def sycall(*args)
          super
        end

        def open(*args)
          super
        end

        def puts(*args)
          super
        end

        def gets(*args)
          super
        end

        def readline(*args)
          super
        end

        def readlines(*args)
          super
        end
      end

      module Random
        def srand(*args)
          super
        end

        def rand(*args)
          super
        end
      end

      module Signal
        def trap(*args)
          super
        end
      end

      module Mutex
        def synchronize(*args)
          super
        end

        def lock(*args)
          super
        end

        def unlock(*args)
          super
        end

        def sleep(*args)
          super
        end
      end
    end
  end
end

Object.prepend Raygun::Apm::Hooks::Object
IO.prepend Raygun::Apm::Hooks::IO
Random.prepend Raygun::Apm::Hooks::Random
Signal.prepend Raygun::Apm::Hooks::Signal
Thread::Mutex.prepend Raygun::Apm::Hooks::Mutex