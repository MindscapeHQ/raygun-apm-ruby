#!/bin/bash
set -e

export HOME=/usr/local/home

REQUIRED_MAJOR=3
REQUIRED_MINOR=1

install_ruby() {
  echo "Installing Ruby ${REQUIRED_MAJOR}.${REQUIRED_MINOR}..."
  
  # Install rbenv if not present
  if ! command -v rbenv &> /dev/null; then
    echo "Installing rbenv..."
    curl -fsSL https://github.com/rbenv/rbenv-installer/raw/HEAD/bin/rbenv-installer | bash
  fi
  
  export PATH="$HOME/.rbenv/bin:$HOME/.rbenv/shims:$PATH"
  eval "$(rbenv init - bash)"
  
  # Install Ruby
  rbenv install -s ${REQUIRED_MAJOR}.${REQUIRED_MINOR}.0
  rbenv global ${REQUIRED_MAJOR}.${REQUIRED_MINOR}.0
  rbenv rehash
  
  echo "Ruby installed: $(ruby -v)"
}

# Check Ruby version using Ruby itself (no bc dependency)
needs_install=false
if command -v ruby &> /dev/null; then
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

if [ "$needs_install" = true ]; then
  echo "Ruby version too old or missing, need >= ${REQUIRED_MAJOR}.${REQUIRED_MINOR}"
  install_ruby
  export PATH="$HOME/.rbenv/bin:$HOME/.rbenv/shims:$PATH"
  eval "$(rbenv init - bash)"
fi

echo "Using Ruby: $(ruby -v)"

# Ensure bundler is installed
gem install bundler --no-document --conservative

# Install dependencies and build
bundle config set --local path vendor/cache
bundle install
bundle exec rake gem:native