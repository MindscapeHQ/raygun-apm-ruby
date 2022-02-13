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

Raygun::Apm::Tracer.patch(Object, Raygun::Apm::Hooks::Object)
Raygun::Apm::Tracer.patch(IO, Raygun::Apm::Hooks::IO)
Raygun::Apm::Tracer.patch(Random, Raygun::Apm::Hooks::Random)
Raygun::Apm::Tracer.patch(Signal, Raygun::Apm::Hooks::Signal)
Raygun::Apm::Tracer.patch(Thread::Mutex, Raygun::Apm::Hooks::Mutex)
