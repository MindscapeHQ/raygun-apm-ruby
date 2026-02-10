#!/bin/bash
set -e

export HOME=/usr/local/home

# Use rbenv Ruby if available
if [ -d "$HOME/.rbenv" ]; then
  export PATH="$HOME/.rbenv/bin:$HOME/.rbenv/shims:$PATH"
fi

echo "Using Ruby: $(ruby -v)"

# Run tests
bundle exec rake test
