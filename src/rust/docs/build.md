# Building Rust code

`yb_build.sh` builds it like any other target:

```bash
./yb_build.sh release --target yb-master-rs --sj --skip-pg-parquet --no-odyssey --no-ybc
./yb_build.sh release --target rust_checks   # rustfmt, clippy -D warnings, cargo test
```

`--skip-rust` leaves out `src/rust`, `src/yb/rust_bridge` and every component's C++ host.

## Toolchain

`src/rust/rust-toolchain.toml` pins the Rust version. With rustup, Cargo picks it up automatically;
without rustup, the installed `cargo` must be that version. `cmake_modules/YugabyteRust.cmake`
fails the configure step otherwise.

Crates come from crates.io. `Cargo.lock` is committed and every build passes `--locked`, so a build
never resolves new versions. Direct dependencies are pinned exactly in the workspace `Cargo.toml`.
An offline vendor archive is required before the Rust master merges to main.

## How CMake drives Cargo

CMake owns every final link, so sanitizer runtimes, tcmalloc, rpaths and the post-build steps work
unchanged. Cargo builds three kinds of output:

- **One static library per executable.** `yb-master-rs` links `libmaster_server.a`, built from
  `master/server`, which depends on every other crate it needs. Two Rust static libraries in one
  executable would each carry the Rust standard library.
- **Build tools.** `yb-cxxgen` writes the C++ half of every `#[cxx::bridge]` into
  `$BUILD_ROOT/rust/cxxgen/<path>.rs.{h,cc}`. CMake compiles those with the normal flags: shared
  bridges into `yb_rust_bridge_obj` (`src/yb/rust_bridge`), a component's own bridges into its host
  (`src/yb/master_host`). A new bridge file must be added to the `yb_rust_cxxgen` list there.
- **Test tools** (`yb_add_rust_tool`), copied into the build's `bin/`.

The Cargo step runs on every build and Cargo decides what is stale. The library is a CMake
byproduct, so a no-op Cargo run does not relink.

Cargo runs with inherited `CFLAGS`/`CXXFLAGS` removed and with yb's clang and libc++. The only C++
Cargo itself compiles is the cxx runtime (`cxx.cc`), and it must use the same standard library as
the code that receives `rust::String` values. `openssl-sys` is pointed at the third-party OpenSSL
(`OPENSSL_INCLUDE_DIR`, `OPENSSL_LIB_DIR`), the one the C++ code links; an executable linking a
Rust library that uses OpenSSL lists `openssl` in its CMake link libraries. To run `cargo` by hand,
set those two variables, and `LD_LIBRARY_PATH` to the library directory for `cargo test`.

Output directories under the build root:

| Path | Contents |
|---|---|
| `rust/target/` | Cargo target dir for the static libraries |
| `rust/tools/` | Cargo target dir for build and test tools |
| `rust/cxxgen/` | Generated bridge C++ and `rust/cxx.h` |
