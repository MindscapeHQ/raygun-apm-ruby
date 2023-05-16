export HOME=/usr/local/home
stdbuf -o0 bundle config path vendor/cache
stdbuf -o0 bundle install
stdbuf -o0 bundle exec rake gem:native