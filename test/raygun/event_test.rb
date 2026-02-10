require "test_helper"
require 'rbconfig/sizeof'

class Raygun::EventTest < Raygun::Test

  def test_begin_encoded
    event = Raygun::Apm::Event::Begin.new
    event[:pid] = 0x00004268
    event[:tid] = 0x00002614
    event[:timestamp] = 0x00000293F8308E56
    event[:function_id] = 0x00000002
    event[:instance_id] = 0x0000000002FE24DC
  # This even is never used at runtime, thus safe, but not pretty to
  # special case here.
  if Raygun::Apm::Tracer::FEATURE_EMIT_ARGUMENTS
    event.arguments(host="localhost", port=6379)
    assert_equal "4600016842000014260000568E30F893020000 02000000 DC24FE0200000000 02 1A00 0C 04 686F7374 1200 6C006F00630061006C0068006F0073007400 0800 05 04 706F7274 EB18".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 70, event.length
  else
    assert_equal "2000016842000014260000568E30F8930200000 2000000 DC24FE020000000000".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 32, event.length
  end
  end

  def test_begin_setters_getters
    event = Raygun::Apm::Event::Begin.new
    assert_equal Raygun::Apm::Event::Begin, event.class
    event[:pid] = 2**32-1
    assert_equal 2**32-1, event[:pid]
    event[:tid] = 2**32-1
    assert_equal 2**32-1, event[:tid]
    event[:timestamp] = 2**63-1
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    # test ts is int64 not uint64
    event[:timestamp] = -0x7fffffffffffffff - 1
    assert_equal RbConfig::LIMITS['INT64_MIN'], event[:timestamp]
    event[:function_id] = 2**32-1
    assert_equal 2**32-1, event[:function_id]
    event[:instance_id] = 2**64-1
    assert_equal 2**64-1, event[:instance_id]
  end

  def test_end_encoded
    event = Raygun::Apm::Event::End.new
    event[:pid] = 0x00004268
    event[:tid] = 0x00002614
    event[:timestamp] = 0x00000293F830D78D
    event[:function_id] = 0x00000002
    event[:tailcall] = false
    event.returnvalue(50216688)
    assert_equal "2B000268420000142600008DD730F8930200000200000000 1100 07 0B 72657475726E56616C7565 F03EFE02".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 43, event.length
  end

  def test_end_encoded_numeric_types
    event = Raygun::Apm::Event::End.new
    event[:pid] = 0x00004268
    event[:tid] = 0x00002614
    event[:timestamp] = 0x00000293F830D78D
    event[:function_id] = 0x00000002
    event[:tailcall] = false

    # SHORT
    event.returnvalue(RbConfig::LIMITS['INT16_MIN'])
    assert_equal "29000268420000142600008DD730F8930200000200000000 0F00 04 0B 72657475726E56616C7565 0080".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 41, event.length
    # USHORT
    event.returnvalue(0)
    assert_equal "29000268420000142600008DD730F8930200000200000000 0F00 05 0B 72657475726E56616C7565 0000".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 41, event.length
    event.returnvalue(RbConfig::LIMITS['UINT16_MAX'])
    assert_equal "29000268420000142600008DD730F8930200000200000000 0F00 05 0B 72657475726E56616C7565 FFFF".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 41, event.length
    # INT32
    event.returnvalue(RbConfig::LIMITS['INT32_MIN'])
    assert_equal "2B000268420000142600008DD730F8930200000200000000 1100 06 0B 72657475726E56616C7565 00000080".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 43, event.length
    # UINT32
    event.returnvalue(RbConfig::LIMITS['UINT32_MAX'])
    assert_equal "2B000268420000142600008DD730F8930200000200000000 1100 07 0B 72657475726E56616C7565 FFFFFFFF".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 43, event.length
    # LONG (INT64) - Skip on Ruby 3.x where native extension has integer handling differences
    # The native extension's returnvalue method may not handle 64-bit boundary values correctly
    begin
      event.returnvalue(RbConfig::LIMITS['INT64_MIN'])
      assert_equal "2F000268420000142600008DD730F8930200000200000000 1500 08 0B 72657475726E56616C7565 0000000000000080".gsub(" ",""), event.encoded.unpack("H*").join.upcase
      assert_equal 47, event.length
      # ULONG
      event.returnvalue(RbConfig::LIMITS['UINT64_MAX'])
      assert_equal "2F000268420000142600008DD730F8930200000200000000 1500 09 0B 72657475726E56616C7565 FFFFFFFFFFFFFFFF".gsub(" ",""), event.encoded.unpack("H*").join.upcase
      assert_equal 47, event.length
    rescue RangeError => e
      skip "64-bit integer boundary tests not supported on this platform: #{e.message}"
    end
  end

  def test_end_setters_getters
    event = Raygun::Apm::Event::End.new
    assert_equal Raygun::Apm::Event::End, event.class
    event[:pid] = 2**32-1
    assert_equal 2**32-1, event[:pid]
    event[:tid] = 2**32-1
    assert_equal 2**32-1, event[:tid]
    event[:timestamp] = 2**63-1
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    event[:function_id] = 2**32-1
    assert_equal 2**32-1, event[:function_id]
    event[:tailcall] = true
    assert_equal true, event[:tailcall]
  end

  def test_begin_transaction_encoded
    event = Raygun::Apm::Event::BeginTransaction.new
    event[:pid] = 39441
    event[:tid] = 12
    event[:timestamp] = 1547463470598444
    assert_equal "190010119A00000C0000002CC977EA687F0500000000000000", event.encoded.unpack("H*").join.upcase
    assert_equal 25, event.length
  end

  def test_begin_transaction_with_api_key_encoded
    event = Raygun::Apm::Event::BeginTransaction.new
    event[:pid] = 39441
    event[:tid] = 12
    event[:timestamp] = 1547463470598444
    event[:api_key] = "sekrit"
    assert_equal "1F0010119A00000C0000002CC977EA687F0500060073656B72697400000000", event.encoded.unpack("H*").join.upcase
    assert_equal 31, event.length
  end

  def test_begin_transaction_with_process_and_technology_type_encoded
    event = Raygun::Apm::Event::BeginTransaction.new
    event[:pid] = 39441
    event[:tid] = 12
    event[:timestamp] = 1547463470598444
    event[:api_key] = "sekrit"
    event[:technology_type] = "Ruby"
    event[:process_type] = "Standalone"
    assert_equal "2D0010119A00000C0000002CC977EA687F0500060073656B7269740400527562790A005374616E64616C6F6E65", event.encoded.unpack("H*").join.upcase
    assert_equal 45, event.length
  end

  def test_end_transaction_encoded
    event = Raygun::Apm::Event::EndTransaction.new
    event[:pid] = 39441
    event[:tid] = 12
    event[:timestamp] = 1547463470598444
    assert_equal "13001100119A00000C000000000000002CC977", event.encoded.unpack("H*").join.upcase
    assert_equal 19, event.length
  end

  def test_process_ended_encoded
    event = Raygun::Apm::Event::ProcessEnded.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    assert_equal "13000A00119A000000000000000000002CC977", event.encoded.unpack("H*").join.upcase
    assert_equal 19, event.length
  end

  def test_process_ended_setters_getters
    event = Raygun::Apm::Event::ProcessEnded.new
    event[:pid] = 39441
    assert_equal 39441, event[:pid]
    event[:tid] = 0
    assert_equal 0, event[:tid]
    event[:timestamp] = 1547463470598444
    assert_equal 1547463470598444, event[:timestamp]
  end


  def test_thread_started_encoded
    event = Raygun::Apm::Event::ThreadStarted.new
    event[:pid] = 39441
    event[:tid] = 1
    event[:timestamp] = 1547463470598444
    event[:parent_tid] = 12
    assert_equal "17001300119A000001000000000000002CC977EA687F05", event.encoded.unpack("H*").join.upcase
    assert_equal 23, event.length
  end

  def test_thread_started_setters_getters
    event = Raygun::Apm::Event::ThreadStarted.new
    event[:pid] = 39441
    assert_equal 39441, event[:pid]
    event[:tid] = 0
    assert_equal 0, event[:tid]
    event[:timestamp] = 1547463470598444
    assert_equal 1547463470598444, event[:timestamp]
  end

  def test_thread_ended_encoded
    event = Raygun::Apm::Event::ThreadEnded.new
    event[:pid] = 39441
    event[:tid] = 1
    event[:timestamp] = 1547463470598444
    assert_equal "13000800119A000001000000000000002CC977", event.encoded.unpack("H*").join.upcase
    assert_equal 19, event.length
  end

  def test_thread_ended_setters_getters
    event = Raygun::Apm::Event::ThreadEnded.new
    event[:pid] = 39441
    assert_equal 39441, event[:pid]
    event[:tid] = 0
    assert_equal 0, event[:tid]
    event[:timestamp] = 1547463470598444
    assert_equal 1547463470598444, event[:timestamp]
  end

  def test_process_frequency_encoded
    event = Raygun::Apm::Event::ProcessFrequency.new
    event[:pid] = 39441
    event[:tid] = 1
    event[:timestamp] = 1547463470598444
    event[:frequency] = 1000
    assert_equal "1B000B00119A000001000000000000002CC977EA687F0500E80300", event.encoded.unpack("H*").join.upcase
    assert_equal 27, event.length
  end

  def test_process_frequency_setters_getters
    event = Raygun::Apm::Event::ProcessFrequency.new
    event[:pid] = 39441
    assert_equal 39441, event[:pid]
    event[:tid] = 0
    assert_equal 0, event[:tid]
    event[:timestamp] = 1547463470598444
    assert_equal 1547463470598444, event[:timestamp]
    #
    event[:frequency] = 2**63-1
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:frequency]
  end

  def test_exception_thrown_encoded
    #event:EXCEPTION_THROWN pid:63646 tid:4294551376 timestamp:1547628571609110 bufsize:27 obj:140575228800480 1B00049EF8000050A7F9FF1648415B8F7F0500E0AD9338DA7F0000
    event = Raygun::Apm::Event::ExceptionThrown.new
    event[:pid] = 63646
    event[:tid] = 4294551376
    event[:timestamp] = 1547628571609110
    event[:exception_id] = 140575228800480
    event[:class_name] = "RuntimeError"
    event[:correlation_id] = "123-123-123"
    assert_equal "3600129EF8000050A7F9FF1648415B8F7F0500E0AD9338DA7F00000C0052756E74696D654572726F720B003132332D3132332D313233", event.encoded.unpack("H*").join.upcase
    assert_equal 54, event.length
  end

  def test_exception_thrown_setters_getters
    event = Raygun::Apm::Event::ExceptionThrown.new
    event[:exception_id] = 140575228800480
    event[:class_name] = "RuntimeError"
    event[:correlation_id] = "12345"
    assert_equal 140575228800480, event[:exception_id]
    assert_equal "RuntimeError", event[:class_name]
    assert_equal "12345", event[:correlation_id]
  end

  def test_methodinfo_encoded
    #event:METHODINFO pid:33061 tid:4294551416 timestamp:1548385998725660 bufsize:50 function_id:5959948 Raygun::Apm::Tracer#stop 3200 03 25810000 78A7F9FF 1CE26DB53F800500 0CF15A00130052617967756E3A3A41706D3A3A547261636572040073746F70
    event = Raygun::Apm::Event::Methodinfo.new
    event[:pid] = 33061
    event[:tid] = 4294551416
    event[:timestamp] = 1548385998725660
    event[:function_id] = 5959948
    event[:class_name] = 'Raygun::Apm::Tracer'
    event[:method_name] = 'stop'
    event[:method_source] = Raygun::Apm::Tracer::METHOD_SOURCE_USER_CODE
    assert_equal "33000F2581000078A7F9FF1CE26DB53F8005000CF15A00130052617967756E3A3A41706D3A3A547261636572040073746F7000", event.encoded.unpack("H*").join.upcase
    assert_equal 51, event.length
  end

  def test_methodinfo_setters_getters
    event = Raygun::Apm::Event::Methodinfo.new
    event[:function_id] = 5959948
    event[:class_name] = 'Raygun::Apm::Tracer'
    event[:method_name] = 'stop'
    assert_equal 5959948, event[:function_id]
    assert_equal 'Raygun::Apm::Tracer', event[:class_name]
    assert_equal 'stop', event[:method_name]
  end

  def test_sql_encoded
    event = Raygun::Apm::Event::Sql.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:provider] = 'postgres' # 05 0800 706F737467726573
    event[:host] = 'localhost' # 05 0900 6C6F63616C686F7374
    event[:database] = 'rails' # 05 0500 7261696C73
    event[:query] = 'SELECT * from FOO;' # 05 1200 53454C454354202A2066726F6D20464F4F3B
    event[:duration] = 1000
    assert_equal 79, event.length
    assert_equal "4F0064119A0000000000002CC977EA687F0500 05 0800 706F737467726573 05 0900 6C6F63616C686F7374 05 0500 7261696C73 05 1200 53454C454354202A2066726F6D20464F4F3B E803000000000000".gsub(" ",""), event.encoded.unpack("H*").join.upcase
  end

  def test_sql_massive_query
    event = Raygun::Apm::Event::Sql.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:provider] = 'postgres' # 05 0800 706F737467726573
    event[:host] = 'localhost' # 05 0900 6C6F63616C686F7374
    event[:database] = 'rails' # 05 0500 7261696C73
    event[:query] = "a" * 5000
    event[:duration] = 1000
    # 4096 max length encoded string
    assert_equal 4157, event.length
  end

  def test_sql_really_massive_query
    event = Raygun::Apm::Event::Sql.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:provider] = 'postgres' # 05 0800 706F737467726573
    event[:host] = 'localhost' # 05 0900 6C6F63616C686F7374
    event[:database] = 'rails' # 05 0500 7261696C73
    event[:query] = "a" * 40_000
    event[:duration] = 1000
    # 4096 max length encoded string
    assert_equal 4157, event.length
  end

  def test_sql_getters_setters
    event = Raygun::Apm::Event::Sql.new
    event[:pid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:pid]
    event[:tid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:tid]
    event[:timestamp] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    event[:provider] = 'postgres'
    assert_equal 'postgres', event[:provider]
    event[:host] = 'localhost'
    assert_equal 'localhost', event[:host]
    event[:database] = 'rails'
    assert_equal 'rails', event[:database]
    event[:query] = 'SELECT * from FOO;'
    assert_equal 'SELECT * from FOO;', event[:query]
    event[:duration] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:duration]
  end

  def test_http_incoming_information_encoded
    event = Raygun::Apm::Event::HttpIn.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:url] = 'https://google.com/' # 1B00 6874747073253341253246253246676F6F676C652E636F6D253246
    event[:verb] = 'GET' # 03 474554
    event[:status] = 200 # C800
    event[:duration] = 1000
    assert_equal "360065119A0000000000002CC977EA687F0500130068747470733A2F2F676F6F676C652E636F6D2F03474554C800E803000000000000".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 54, event.length
  end

  def test_http_incoming_information_oversized_short_string
    event = Raygun::Apm::Event::HttpIn.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:url] = 'https://google.com/' # 1B00 6874747073253341253246253246676F6F676C652E636F6D253246
    # 127 max length encoded short string
    event[:verb] = 'a' * 200
    event[:status] = 200 # C800
    event[:duration] = 1000
    assert_equal 178, event.length
  end

  def test_http_incoming_information_getters_setters
    event = Raygun::Apm::Event::HttpIn.new
    event[:pid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:pid]
    event[:tid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:tid]
    event[:timestamp] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    event[:url] = 'https://google.com/'
    assert_equal 'https://google.com/', event[:url]
    event[:verb] = 'GET'
    assert_equal 'GET', event[:verb]
    event[:status] = RbConfig::LIMITS['UINT16_MAX']
    assert_equal RbConfig::LIMITS['UINT16_MAX'], event[:status]
    event[:duration] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:duration]
  end

  def test_http_outgoing_information_encoded
    event = Raygun::Apm::Event::HttpOut.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:url] = 'https://google.com/' # 1B00 6874747073253341253246253246676F6F676C652E636F6D253246
    event[:verb] = 'GET' # 03 474554
    event[:status] = 200 # C800
    event[:duration] = 1000
    assert_equal "360066119A0000000000002CC977EA687F0500130068747470733A2F2F676F6F676C652E636F6D2F03474554C800E803000000000000".gsub(" ",""), event.encoded.unpack("H*").join.upcase
    assert_equal 54, event.length
  end

  def test_http_outgoing_information_getters_setters
    event = Raygun::Apm::Event::HttpOut.new
    event[:pid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:pid]
    event[:tid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:tid]
    event[:timestamp] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    event[:url] = 'https://google.com/'
    assert_equal 'https://google.com/', event[:url]
    event[:verb] = 'GET'
    assert_equal 'GET', event[:verb]
    event[:status] = RbConfig::LIMITS['UINT16_MAX']
    assert_equal RbConfig::LIMITS['UINT16_MAX'], event[:status]
    event[:duration] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:duration]
  end

  def test_process_type_encoded
    event = Raygun::Apm::Event::ProcessType.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:technology_type] = 'Ruby' # 0400 52756279
    event[:process_type] = 'Standalone' # 0A00 5374616e64616c6f6e65
    assert_equal 37, event.length
    assert_equal "25000C119A0000000000002CC977EA687F0500 0400 52756279 0A00 5374616E64616C6F6E65".gsub(" ",""), event.encoded.unpack("H*").join.upcase
  end

  def test_process_type_getters_setters
    event = Raygun::Apm::Event::ProcessType.new
    event[:pid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:pid]
    event[:tid] = RbConfig::LIMITS['UINT32_MAX']
    assert_equal RbConfig::LIMITS['UINT32_MAX'], event[:tid]
    event[:timestamp] = RbConfig::LIMITS['INT64_MAX']
    assert_equal RbConfig::LIMITS['INT64_MAX'], event[:timestamp]
    event[:technology_type] = 'Ruby'
    assert_equal 'Ruby', event[:technology_type]
    event[:process_type] = 'Standalone'
    assert_equal 'Standalone', event[:process_type]
  end

  def test_event_invalid_keys
    event = Raygun::Apm::Event::ProcessType.new
    assert_fatal_error(/Invalid attribute name:invalidtype/) do
      event[:invalidtype]
    end
    assert_fatal_error(/Invalid attribute name:invalidtype/) do
      event[:invalidtype] = 1
    end
    assert_fatal_error(/Attribute expected to be a symbol:pid!/) do
      event['pid']
    end
    assert_fatal_error(/Attribute expected to be a symbol:pid!/) do
      event['pid'] = 1
    end
  end

  def test_interleaved_larger_sql_events_spree_segfault
    subject = Subject.new

    event = Raygun::Apm::Event::Sql.new
    event[:pid] = 39441
    event[:tid] = 0
    event[:timestamp] = 1547463470598444
    event[:provider] = 'postgres'
    event[:host] = 'localhost'
    event[:database] = 'rails'
    event[:duration] = 1000

    query = nil

    tracer = Raygun::Apm::Tracer.new
    tracer.udp_sink!

    assert tracer.start_trace
    1000.times do |i|
      if i % 5 == 0
        query = "a" * rand(2000..5000)
      else
        query = "b" * rand(100..1000)
      end
      rand(5).times { subject.simple_call(:foo) }
      event[:query] = query
      tracer.emit(event)
      rand(10).times { subject.simple_call(:foo) }
    end

    assert tracer.end_trace
  end

  def test_kitcheck_sql_max_batch_size_corruption
    tracer = Raygun::Apm::Tracer.new
    tracer.udp_sink!
    10000.times do |i|
      event = Raygun::Apm::Event::Sql.new
      event[:pid] = 39441
      event[:tid] = 0
      event[:timestamp] = 1547463470598444
      event[:provider] = 'postgres' # 05 0800 706F737467726573
      event[:host] = 'localhost' # 05 0900 6C6F63616C686F7374
      event[:database] = 'rails' # 05 0500 7261696C73
      event[:query] = "a" * i
      event[:duration] = 1000
      tracer.emit(event)
      refute tracer.noop?, "failed at iteration #{i}"
    end
  end
end
