class SimpleController < ApplicationController
    def index
    end    

    def index_sleep
      sleep 0.5
    end

    def index_sql
    end

    def index_http
      Net::HTTP.get URI("http://www.google.com")
    end
end
