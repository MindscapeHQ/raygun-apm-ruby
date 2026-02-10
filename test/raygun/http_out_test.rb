require "test_helper"
require "faraday"
require 'net/http/post/multipart'
require 'rest-client'
require 'excon'
require 'httparty'
require 'httpclient'

class Raygun::HttpOutTest < Raygun::Test

  def test_raw
    events = apm_trace do
      Net::HTTP.get_response('raygun.com', '/')
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'Net::HTTP' }
    assert_equal 'get_response', methodinfo[:method_name]
    assert_equal Raygun::Apm::Tracer::METHOD_SOURCE_KNOWN_LIBRARY, methodinfo[:method_source]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    assert_equal 'http://raygun.com/', http_out[:url]
    assert_equal 301, http_out[:status]
  end

  def test_faraday
    events = apm_trace do
      Faraday.get 'http://www.google.com'
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'Faraday::Connection' }
    refute_nil methodinfo, "Expected Faraday::Connection methodinfo event"
    assert_equal 'get', methodinfo[:method_name]

    # Note: Object#sleep and Net::HTTP#get methodinfo events may not be captured in all Ruby versions
    methodinfos = events.select{|e| Raygun::Apm::Event::Methodinfo === e }
    assert methodinfos.length >= 1, "Expected at least one methodinfo event"

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    refute_nil http_out, "Expected HttpOut event to be captured"
    assert_equal 'http://www.google.com/', http_out[:url]
    assert_equal 200, http_out[:status]
  end

  def test_multipart_post
    events = apm_trace do
      url = URI.parse('http://www.example.com/upload')
      req = Net::HTTP::Post::Multipart.new(url.path, {})
      res = Net::HTTP.start(url.host, url.port) do |http|
        http.request(req)
      end
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'Multipartable' }
    assert_equal 'initialize', methodinfo[:method_name]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'POST' }
    assert_equal 'POST', http_out[:verb]
    assert_equal 'http://www.example.com/upload', http_out[:url]
    assert_includes [404, 405], http_out[:status]
  end

  def test_rest_client
    events = apm_trace do
      RestClient.get 'http://www.google.com'
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'RestClient' }
    assert_equal 'get', methodinfo[:method_name]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    refute_nil http_out, "Expected HttpOut event to be captured"
    assert_equal 'http://www.google.com', http_out[:url]
    assert_equal 200, http_out[:status]
  end

  def test_excon
    events = apm_trace do
      Excon.get('http://google.com')
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'Excon' }
    assert_equal 'get', methodinfo[:method_name]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    assert_equal 'http://google.com/', http_out[:url]
    assert_equal 301, http_out[:status]
  end

  def test_http_party
    events = apm_trace do
      HTTParty.get('http://google.com')
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'HTTParty' }
    assert_equal 'get', methodinfo[:method_name]

    http_outs = events.select{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    assert http_outs.length >= 1, "Expected at least one HttpOut event to be captured"
    http_out_redirect, http_out = http_outs

    assert_equal 'http://google.com/', http_out_redirect[:url]
    assert_equal 301, http_out_redirect[:status]

    if http_out
      assert_equal 'http://www.google.com/', http_out[:url]
      assert_equal 200, http_out[:status]
    end
  end

  # TODO: does not reply on Net::HTTP - hook with a specific implementation (no HTTP OUT event spawned)
  def test_httpclient
    events = apm_trace do
      client = HTTPClient.new
      client.get('http://raygun.io', "index.html")
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'HTTPClient' }
    assert_equal 'get', methodinfo[:method_name]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'GET' }
    refute_nil http_out, "Expected HttpOut GET event to be captured"
    assert_equal 'http://raygun.io/index.html', http_out[:url]
    assert_equal 301, http_out[:status]

    # Test case for POST as well
    events = apm_trace do
      client = HTTPClient.new
      client.post('http://raygun.io', query: nil, body: '')
    end

    methodinfo = events.detect{|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == 'HTTPClient' }
    assert_equal 'post', methodinfo[:method_name]

    http_out = events.detect{|e| Raygun::Apm::Event::HttpOut === e && e[:verb] == 'POST' }
    refute_nil http_out, "Expected HttpOut POST event to be captured"
    assert_equal 'http://raygun.io', http_out[:url]

    # https://github.com/MindscapeHQ/heroku-buildpack-raygun-apm/issues/6
    client = HTTPClient.new
    client.send(:do_request, :post, URI('http://raygun.io'), nil, '', {})

    # Raw non-Rails URI behavior expectations (String only)
    assert_equal "http://raygun.io/bar", client.send(:raygun_apm_url, 'http://raygun.io', "bar")
    assert_raises ArgumentError do
      assert_equal "http://raygun.io/bar", client.send(:raygun_apm_url, 'http://raygun.io', {"foo" => "bar"})
    end

    # Rails specific overrides that forces .to_param on objects, if the object has such a method defined
    h = {}
    def h.to_param
      "bar"
    end

    assert_equal "http://raygun.io/bar", client.send(:raygun_apm_url, 'http://raygun.io', h)
  end
end
