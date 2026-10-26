# Testing the Rust master

## Rust unit tests

`./yb_build.sh <type> --target rust_checks` runs `cargo fmt --check`, `cargo clippy -D warnings` and
`cargo test` for the workspace. `master` and `yb-cpp-host` are excluded from `cargo test`: their
code calls into the C++ host, so it only links inside `yb-master-rs` and is covered by C++ tests.
