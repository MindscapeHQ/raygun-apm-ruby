export HOME=/usr/local/home
# Update bundler version in lockfile to match installed bundler
stdbuf -o0 bundle update --bundler || true
stdbuf -o0 bundle config path vendor/cache
stdbuf -o0 bundle install
stdbuf -o0 bundle exec rake gem:native