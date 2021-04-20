class Subject
    attr_reader :event, :stack_frames

    def initialize
      @event = nil
      @stack_frames = 0
    end

    def call(event)
      @event = event
    end

    def simple_call(arg)
    end

    def blacklist1;end
    def blacklist2;end

    def string_arg_return_nil(arg = "foobar")
    end

    def exception_raised
      raise 'exception'
    end

    def boolean_return(bool = true)
      true
    end

    def block_method
      yield
    end

    def string_return(host = "localhost")
      host
    end

    def binary_string_return
      ["deadbeef"].pack("H*")
    end

    def largestring_return(largestring = ("foo" * 2100))
      "foo" * 2100
    end

    def invalidencoding_string_return(invalidencoding = "\u0000\xD8\u0000\u0000")
      #[55296].pack('L').force_encoding('UTF-8')
      invalidencoding
    end

    def float_return(float = 2.33)
      2.33
    end

    def symbol_return(symbol = :symbol)
      :symbol
    end

    def complex_method(a, b = 1, *c, d: 1, **x)
      p a, b, c, d, x
    end

    def catch_all(*)
    end

    def method_missing(*)
    end

    def bignum_return
      1 << (1 << 16)
    end

    alias original_require require
    def require(path)
      original_require(path)
    end

    def truncated_variable_name(truncated_var_name = 'truncated_var_name' * 50)
    end

    def default_scope_override
    end

    def before_add_for_comments
    end

    def _reflections
    end

    # Results in VM raising SystemStackError, roughly 11ksomething frames
    def recursive
      @stack_frames += 1
      recursive
    end

    # Circuit breaker for VM SystemStackError to exit at an arbitrary max_depth limit
    def recursive_limited(max_depth)
      @stack_frames += 1
      return if @stack_frames == max_depth
      recursive_limited(max_depth)
    end

    # Non-recursive nested methods deep stack
    300.times do |i|
      define_method "deep_stack_method#{i}" do
        send("deep_stack_method#{i + 1}")
      end
    end

    # Unicode tests
    def ★★★
      :stars
    end

    def 🔪
      "🥩 "
    end
end
