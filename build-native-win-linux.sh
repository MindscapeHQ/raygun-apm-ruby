#!/bin/bash
set -e

export HOME=/usr/local/home

REQUIRED_MAJOR=3
REQUIRED_MINOR=1

setup_rbenv_path() {
  export PATH="$HOME/.rbenv/bin:$HOME/.rbenv/shims:$PATH"
}

install_ruby() {
  echo "Installing Ruby ${REQUIRED_MAJOR}.${REQUIRED_MINOR}..."
  
  # Install build dependencies
  echo "Installing build dependencies..."
  apt-get update -qq
  apt-get install -y -qq autoconf bison build-essential libssl-dev libyaml-dev libreadline-dev zlib1g-dev libncurses5-dev libffi-dev libgdbm-dev
  
  # Install rbenv if not present
  if [ ! -d "$HOME/.rbenv" ]; then
    echo "Installing rbenv..."
    git clone https://github.com/rbenv/rbenv.git "$HOME/.rbenv"
    git clone https://github.com/rbenv/ruby-build.git "$HOME/.rbenv/plugins/ruby-build"
  fi
  
  setup_rbenv_path
  
  # Install Ruby
  rbenv install -s ${REQUIRED_MAJOR}.${REQUIRED_MINOR}.0
  rbenv global ${REQUIRED_MAJOR}.${REQUIRED_MINOR}.0
  rbenv rehash
  
  echo "Ruby installed: $(ruby -v)"
}

# Check Ruby version using Ruby itself (no bc dependency)
needs_install=false
if command -v ruby > /dev/null 2>&1; then
  RUBY_MAJOR=$(ruby -e "puts RUBY_VERSION.split('.')[0].to_i")
  RUBY_MINOR=$(ruby -e "puts RUBY_VERSION.split('.')[1].to_i")
  echo "Found Ruby: $(ruby -v)"
  
  if [ "$RUBY_MAJOR" -lt "$REQUIRED_MAJOR" ]; then
    needs_install=true
  elif [ "$RUBY_MAJOR" -eq "$REQUIRED_MAJOR" ] && [ "$RUBY_MINOR" -lt "$REQUIRED_MINOR" ]; then
    needs_install=true
  fi
else
  echo "Ruby not found"
  needs_install=true
fi

if [ "$needs_install" = "true" ]; then
  echo "Ruby version too old or missing, need >= ${REQUIRED_MAJOR}.${REQUIRED_MINOR}"
  install_ruby
fi

# Ensure rbenv ruby is in PATH
setup_rbenv_path

echo "Using Ruby: $(ruby -v)"

# Ensure bundler is installed
gem install bundler --no-document --conservative

# Install dependencies and build
bundle config set --local path vendor/cache
bundle install
bundle exec rake gem:native