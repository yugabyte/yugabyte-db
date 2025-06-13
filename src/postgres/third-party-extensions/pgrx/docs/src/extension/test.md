# Testing Extensions with PGRX

Both `cargo test` and `cargo pgrx test` can be used to run tests using the `pgrx-tests` framework.
Tests annotated with `#[pg_test]` will be run inside a Postgres database.
<!-- TODO: explain the test framework more -->

## Guides

- [Memory-Checking (Valgrind, etc.)](./test/memory-checking.md)
