require "test_helper"
require 'rbconfig/sizeof'

class Raygun::RingbufTest < Raygun::Test

    def test_ringbuf_shift_over_capacity
      ringbuf = Raygun::Apm::Ringbuf.new(2)
      refute ringbuf.push [0xDE, 0xAD, 0xBE, 0xEF].pack("CCCC")
      assert_nil ringbuf.shift(1)
    end

    def test_ringbuf_multibyte
      ringbuf = Raygun::Apm::Ringbuf.new(4)
      assert ringbuf.push [0xDE, 0xAD, 0xBE, 0xEF].pack("CCCC")
      assert_equal [0xDE, 0xAD, 0xBE, 0xEF], ringbuf.shift(4).unpack("CCCC")
      assert_nil ringbuf.shift(1)
    end

    def test_ringbuf_single_byte
      ringbuf = Raygun::Apm::Ringbuf.new(4)
      assert ringbuf.push [0xDE].pack("C")
      assert_equal [0xDE], ringbuf.shift(1).unpack("C")
      assert_nil ringbuf.shift(1)
    end

    def test_ringbuf_single_bytes_to_full
      ringbuf = Raygun::Apm::Ringbuf.new(4)
      assert ringbuf.push [0xDE].pack("C")
      assert ringbuf.push [0xAD].pack("C")
      assert ringbuf.push [0xBE].pack("C")
      assert ringbuf.push [0xEF].pack("C")
      assert_equal [0xDE], ringbuf.shift(1).unpack("C")
      assert_equal [0xAD], ringbuf.shift(1).unpack("C")
      assert_equal [0xBE], ringbuf.shift(1).unpack("C")
      assert_equal [0xEF], ringbuf.shift(1).unpack("C")
      assert_nil ringbuf.shift(1)
    end

    def test_ringbuf_single_bytes_to_wraparound
      ringbuf = Raygun::Apm::Ringbuf.new(4)
      assert ringbuf.push [0xDE].pack("C")
      assert ringbuf.push [0xAD].pack("C")
      assert ringbuf.push [0xBE].pack("C")
      assert ringbuf.push [0xEF].pack("C")

      refute ringbuf.push [0xCA].pack("C")
      refute ringbuf.push [0xFE].pack("C")

      assert_equal [0xDE], ringbuf.shift(1).unpack("C")
      assert_equal [0xAD], ringbuf.shift(1).unpack("C")

      assert ringbuf.push [0xCA].pack("C")
      assert ringbuf.push [0xFE].pack("C")
      assert_equal [0xBE], ringbuf.shift(1).unpack("C")
      assert_equal [0xEF], ringbuf.shift(1).unpack("C")
      assert_equal [0xCA], ringbuf.shift(1).unpack("C")
      assert_equal [0xFE], ringbuf.shift(1).unpack("C")

      assert_nil ringbuf.shift(1)
    end

    def test_ringbuf_gc
      ringbuf = Raygun::Apm::Ringbuf.new(4)
      ringbuf = nil
      GC.start
    end
end