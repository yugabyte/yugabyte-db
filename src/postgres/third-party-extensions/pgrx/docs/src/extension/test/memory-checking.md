# Memory Checking

For some background see the writeup in <https://github.com/pgcentralfoundation/pgrx/issues/1217>.

## Running with Memory Checking

### Valgrind

1. Install valgrind, headers, libs (on Fedora `sudo dnf install valgrind valgrind-devel valgrind-tools-devel` is enough).

2. Run `cargo pgrx init --valgrind`. The only major downside to using this as your primary pgrx installation is that its slow, but you can still run without valgrind.

3. Set `USE_VALGRIND` in the environment when running tests, for example `USE_VALGRIND=1 cargo test`. valgrind must be on the path for this to work. This is slow -- taking about 15 minutes on a very beefy cloud server, so manage your timing expectations accordingly.

### Sanitizers

TODO

### Hardened Allocators

For basic usage of electric fence or scudo, `LD_PRELOAD=libefence.so cargo test` or `LD_PRELOAD=libscudo.so cargo test`. More advanced usage (like GWP-ASAN) is still TODO.
