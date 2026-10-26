# Testing the Rust master

## Rust unit tests

`./yb_build.sh <type> --target rust_checks` runs `cargo fmt --check`, `cargo clippy -D warnings` and
`cargo test` for the workspace. `master` and `yb-cpp-host` are excluded from `cargo test`: their
code calls into the C++ host, so it only links inside `yb-master-rs` and is covered by C++ tests.

## Sys-catalog row conformance

The Rust master must serialize every sys-catalog row exactly as the C++ master does (see
[src/rust/docs/proto.md](../../docs/proto.md)). To check a real catalog:

```bash
# With the masters stopped:
build/latest/bin/sys-catalog-tool --fs_data_dirs <master data dir> dump-rows --file rows.tsv
build/latest/bin/master-conformance rows.tsv
```

`dump-rows` writes each row with the C++ runtime's re-serialization. `master-conformance` checks
that Rust produces the same bytes, and that unknown fields of every wire type appended to the row
survive. It exits non-zero on any mismatch.
