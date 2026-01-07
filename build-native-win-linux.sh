export HOME=/usr/local/home

# The gem:native task uses rake-compiler-dock which runs builds inside Docker containers
# with the correct Ruby versions. We just need to invoke it from the host.
# Skip bundle install on host if Ruby version is incompatible - Docker will handle it.

RUBY_MAJOR=$(ruby -e "puts RUBY_VERSION.split('.')[0..1].join('.')")

if [ "$(echo "$RUBY_MAJOR >= 3.0" | bc -l)" = "1" ]; then
  echo "Host Ruby $RUBY_MAJOR is compatible, running bundle install..."
  stdbuf -o0 bundle config path vendor/cache
  stdbuf -o0 bundle install
  stdbuf -o0 bundle exec rake gem:native
else
  echo "Host Ruby $RUBY_MAJOR is older than 3.0, using minimal setup..."
  # Install just the gems needed to run rake gem:native on the host
  gem install rake rake-compiler rake-compiler-dock --no-document || true
  stdbuf -o0 rake gem:native
fi