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

## Integration tests

`src/yb/integration-tests/rust_master-itest.cc` runs the Rust master in a quorum of C++ masters
(ExternalMiniCluster; skipped when `yb-master-rs` is not built):

```bash
./yb_build.sh release --cxx-test rust_master-itest --gtest_filter RustMasterTest.FollowerKeepsSameCatalog
```

| Test | Checks |
|---|---|
| `FollowerKeepsSameCatalog` | A C++ follower restarted as Rust on its own data stays a follower through YCQL DDL, an index, a snapshot schedule and a PITR restore; serves `GetMasterRegistration` and `ListMasters`; answers `NOT_THE_LEADER` to leader-only methods. With the cluster stopped, its sys-catalog rows and `auto_flags_config` equal a C++ follower's, and all rows pass the conformance check. |
| `NeverLeads` | A leader transfer to the Rust master ends with a C++ leader; the Rust master never reports LEADER. |
| `AddedThroughRemoteBootstrap` | A shell-mode Rust master is added with `ChangeConfig`, remote-bootstraps the sys catalog, follows a workload, and is removed. |
| `WebPages` | On the Rust follower's web port, the master list, version, leader-fetched JSON and HTML pages equal a C++ follower's (uptimes and the build-time footer aside); `/masters` bolds the Rust master; `is-leader` is 503; `/varz` and `/prometheus-metrics` come through the proxy; the registration advertises the Rust port. |
| `WebServerHttps` | With the webserver TLS flags the Rust master serves HTTPS with HSTS, refuses plain HTTP, fetches leader pages from an HTTP leader, and advertises the configured interface. |
