export HOME=/usr/local/home
bundle config path vendor/cache
bundle install
bundle exec rake gem:native