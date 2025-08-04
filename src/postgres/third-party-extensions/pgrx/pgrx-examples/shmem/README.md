## Postgres Shared Memory and LWLock Support

Important:
> Extensions that use shared memory **must** be loaded via `postgresql.conf`'s 
>`shared_preload_libraries` configuration setting.

For now, please check out the example in [src/lib.rs](src/lib.rs).  It demonstrates how to
safely use standard Rust types, Rust Atomics, and various data structures from 
[`heapless`](https://crates.io/crates/heapless) via Postgres' shared memory system.

