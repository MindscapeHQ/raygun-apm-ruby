#!/bin/bash
set -e

export HOME=/usr/local/home

# Install Ruby 3.1 if not present or version is too old
REQUIRED_RUBY="3.1"

install_ruby() {
  echo "Installing Ruby ${REQUIRED_RUBY}..."
  
  # Install rbenv if not present
  if ! command -v rbenv &> /dev/null; then
    echo "Installing rbenv..."
    curl -fsSL https://github.com/rbenv/rbenv-installer/raw/HEAD/bin/rbenv-installer | bash
    export PATH="$HOME/.rbenv/bin:$PATH"
    eval "$(rbenv init -)"
  fi
  
  # Install ruby-build plugin if not present
  if [ ! -d "$HOME/.rbenv/plugins/ruby-build" ]; then
    git clone https://github.com/rbenv/ruby-build.git "$HOME/.rbenv/plugins/ruby-build"
  fi
  
  # Install Ruby
  rbenv install -s ${REQUIRED_RUBY}.0
  rbenv global ${REQUIRED_RUBY}.0
  eval "$(rbenv init -)"
}

# Check Ruby version
if command -v ruby &> /dev/null; then
  RUBY_VERSION=$(ruby -e "puts RUBY_VERSION")
  RUBY_MAJOR=$(echo $RUBY_VERSION | cut -d. -f1-2)
  if [ "$(echo "$RUBY_MAJOR < $REQUIRED_RUBY" | bc -l)" = "1" ]; then
    echo "Ruby $RUBY_VERSION is too old, need >= $REQUIRED_RUBY"
    install_ruby
  else
    echo "Ruby $RUBY_VERSION is OK"
  fi
else
  echo "Ruby not found"
  install_ruby
fi

# Ensure bundler is installed
gem install bundler --no-document --conservative

# Install dependencies and build
stdbuf -o0 bundle config path vendor/cache
stdbuf -o0 bundle install
stdbuf -o0 bundle exec rake gem:native