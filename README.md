# raygun-apm

Ruby Profiler for Raygun Application Performance Monitoring.

This gem contains a C native extension that interfaces with the Ruby VM internals for method-level profiling. It communicates with the Raygun APM Agent over TCP/UDP to send trace data.

## Platform Gems

Each release produces **7 precompiled native gems** plus the source gem:

| # | Platform | OS | Architecture | Ruby Versions | Build Method |
|---|----------|----|-------------|---------------|--------------|
| 1 | `x86-mingw32` | Windows | 32-bit | 3.0 | rake-compiler-dock (Docker) |
| 2 | `x64-mingw32` | Windows | 64-bit | 3.0 | rake-compiler-dock (Docker) |
| 3 | `x64-mingw-ucrt` | Windows | 64-bit (UCRT) | 3.1, 3.2 | rake-compiler-dock (Docker) |
| 4 | `x86-linux` | Linux | 32-bit | 3.0, 3.1, 3.2 | rake-compiler-dock (Docker) |
| 5 | `x86_64-linux` | Linux | 64-bit | 3.0, 3.1, 3.2 | rake-compiler-dock (Docker) |
| 6 | `x86_64-darwin` | macOS | Intel | 3.0, 3.1, 3.2 | Native on macOS |
| 7 | `arm64-darwin` | macOS | Apple Silicon | 3.0, 3.1, 3.2 | Native on macOS |

**Note:** Windows Ruby 3.0 uses the `mingw32` platform. Ruby 3.1+ on Windows switched to UCRT (`x64-mingw-ucrt`). That's why there are separate Windows gems.

## Prerequisites

### For development (compile + test on your machine)

- Ruby 3.0, 3.1, or 3.2
- C compiler (GCC 12+ on Linux, Clang/Xcode on macOS)
- `debase-ruby_core_source` >= 3.3.6 (provides Ruby VM header files)
- System packages (Linux): `build-essential`, `libssl-dev`, `zlib1g-dev`, `libyaml-dev`

### For cross-compilation (building all platform gems)

- Docker (for rake-compiler-dock containers — builds Linux and Windows gems)
- macOS machine (for building the two darwin gems natively)
- Multiple Ruby versions installed via ruby-install, rbenv, or similar (for darwin cross-compile)

## Development Setup

```bash
git clone git@github.com:MindscapeHQ/raygun-apm-ruby.git
cd raygun-apm-ruby
bundle install
bundle exec rake compile   # Compile native extension for your current Ruby
bundle exec rake test       # Run the test suite
```

## Building Platform Gems

### Step 1: Build Linux and Windows gems (Docker)

This uses `rake-compiler-dock` to cross-compile inside Docker containers. No Windows or Linux VM needed.

```bash
bundle exec rake gem:native
```

This builds all non-darwin platforms:
- `x86-mingw32` (Windows 32-bit, Ruby 3.0)
- `x64-mingw32` (Windows 64-bit, Ruby 3.0)
- `x64-mingw-ucrt` (Windows 64-bit UCRT, Ruby 3.1+)
- `x86-linux` (Linux 32-bit)
- `x86_64-linux` (Linux 64-bit)

Output goes to `pkg/`.

To build only Linux 64-bit:
```bash
bundle exec rake gem:linux
```

### Step 2: Build macOS gems (native, on a Mac)

Darwin gems **cannot** be cross-compiled in Docker — they must be built on an actual macOS machine.

**Prerequisite:** Install the target Ruby versions (e.g., via ruby-install):
```bash
ruby-install ruby 3.0.7
ruby-install ruby 3.1.7
ruby-install ruby 3.2.9
```

Then build:
```bash
./build-native-macos.sh
# or manually:
bundle exec rake gem:native:darwin
```

This produces:
- `x86_64-darwin` (Intel Mac)
- `arm64-darwin` (Apple Silicon)

Output goes to `pkg/`.

### Step 3: Verify

After both steps, `pkg/` should contain all 7 platform `.gem` files plus the source gem:
```
pkg/
  raygun-apm-1.1.15.pre3.gem              # source gem (compiles on install)
  raygun-apm-1.1.15.pre3-x86-mingw32.gem
  raygun-apm-1.1.15.pre3-x64-mingw32.gem
  raygun-apm-1.1.15.pre3-x64-mingw-ucrt.gem
  raygun-apm-1.1.15.pre3-x86-linux.gem
  raygun-apm-1.1.15.pre3-x86_64-linux.gem
  raygun-apm-1.1.15.pre3-x86_64-darwin.gem
  raygun-apm-1.1.15.pre3-arm64-darwin.gem
```

## Running Tests

```bash
bundle exec rake test          # Full test suite (requires compiled extension)
bundle exec rake compile       # Just compile (no tests)
```

Tests require the native extension to be compiled first (`rake test` does this automatically via the `test => compile` dependency).

**Note:** Some tests (e.g., `apm_test.rb`) attempt to connect to a Raygun APM Agent. Tests that require an agent will raise `FatalError` and are expected to handle this gracefully.

## Release Process

1. Update version in `lib/raygun/apm/version.rb`
2. Build all platform gems (Steps 1 + 2 above)
3. Push each gem to RubyGems:
   ```bash
   for gem in pkg/*.gem; do gem push "$gem"; done
   ```
4. Tag the release:
   ```bash
   git tag v1.1.15.pre3
   git push origin v1.1.15.pre3
   ```

## Project Structure

```
raygun-apm-ruby/
├── ext/raygun/               # C native extension source
│   ├── extconf.rb            # Build configuration (mkmf)
│   ├── raygun_ext.c          # Ruby C API entry point
│   ├── raygun_tracer.c/h     # Core tracer (tracepoints, shadow stack)
│   ├── raygun_encoder.c/h    # Binary event encoding
│   ├── raygun_event.c/h      # Event types (HTTP, SQL, method calls)
│   ├── raygun_platform.c/h   # Platform-specific code
│   ├── raygun_ringbuf.c/h    # Lock-free ring buffer
│   ├── raygun_coercion.c/h   # Ruby value coercion
│   ├── raygun_errors.c/h     # Error handling
│   └── rax.c/h               # Third-party radix tree (for blacklist)
├── lib/raygun/apm/
│   ├── tracer.rb             # Ruby-side tracer (config, hooks, sinks)
│   ├── config.rb             # Environment-based configuration
│   ├── diagnostics.rb        # Agent connectivity checks + noop mode
│   ├── event.rb              # Event type definitions
│   ├── version.rb            # VERSION + MINIMUM_AGENT_VERSION
│   ├── blacklist.rb          # Method blacklist filtering
│   └── hooks/                # Monkey-patches for HTTP clients, Redis, etc.
├── test/raygun/              # Minitest unit tests
├── Rakefile                  # Build tasks (compile, test, gem:native, etc.)
├── build-native-macos.sh     # macOS build shortcut
├── build-native-win-linux.sh # Linux/Windows build (used in Vagrant/CI)
└── Vagrantfile               # Ubuntu VM for Linux builds (alternative to Docker)
```

## Key Concepts

### Noop Mode

When the Raygun APM Agent is running but its version is below `MINIMUM_AGENT_VERSION` (defined in `version.rb`), the tracer enters **noop mode** via `tracer.noop!`. In this mode, all profiling is disabled to avoid overhead. The `raygun-apm-rails` middleware detects this via `tracer.noop?`.

### Native Extension

The C extension hooks into Ruby's TracePoint API and internal VM structures (via `debase-ruby_core_source` headers) to capture method calls, returns, and thread events with minimal overhead. Events are batched and sent to the Raygun Agent over UDP or TCP.

### Compiler Compatibility

- **GCC 12+**: Required `-Wno-use-after-free` for a false positive in third-party `rax.c`. Old-style C function definitions `()` updated to `(void)` for C99 compliance.
- **Clang**: Suppresses Clang-specific warnings (`-Wno-shorten-64-to-32`, etc.) that don't apply to GCC.
- `-Werror` is only enabled when `WERROR=1` or `CI=1` environment variable is set.
