require "test_helper"
require "mongo"

class Raygun::SqlEventTest < Raygun::Test
  def test_mongodb
    if ENV['TEAMCITY_VERSION']
      skip "This test is disabled in CI builds because requires a MongoDB container - coming soon"
      return
    end
    events = apm_trace do
      client = Mongo::Client.new(['127.0.0.1:27017'], :database => 'admin', :user => 'root', :password => 'rootpassword')
      db = client.database
      db.collection_names
    end

    methodinfo = events.detect {|e| Raygun::Apm::Event::Methodinfo === e && e[:class_name] == "Mongo::Operation::Executable" }
    assert_equal "do_execute", methodinfo[:method_name]
    assert_equal 2, methodinfo[:method_source]

    sql = events.detect {|e| Raygun::Apm::Event::Sql === e && e[:provider] == "mongodb" }
    assert_equal "127.0.0.1:27017", sql[:host]
    assert_equal "admin", sql[:database]
    assert sql[:query].include?("admin.listCollections")
  end
end
