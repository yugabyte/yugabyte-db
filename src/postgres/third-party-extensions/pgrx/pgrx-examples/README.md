# pgrx-examples

This directory contains examples of how to work with various aspects of `pgrx`.

- [arrays/](arrays/):  Working with Arrays
- [bad_ideas/](bad_ideas/):  Some "bad ideas" to do in Postgres extensions
- [bgworker/](bgworker/):  A simple Background Worker example
- [bytea/](bytea/):  Working with Postgres' `bytea` type as `Vec<u8>` and `&[u8]` in Rust
- [custom_types/](custom_types/): Create your own custom Postgres types backed by Rust structs/enums
- [errors/](errors/):  Error handling using Postgres or Rust errors/panics
- [operators/](operators/):  Creating operator functions and associated `CREATE OPERATOR/OPERATOR CLASS/OPERATOR FAMILY` DDL
- [shmem/](shmem/):  Postgres Shared Memory support
- [schemas/](schemas/):  How `pgrx` uses Postgres schemas
- [srf/](srf/):  Set-Returning-Functions
- [spi/](spi/):  Using Postgres' Server Programming Interface (SPI)
- [strings/](strings/):  Using Postgres `text`/`varlena` types as Rust `String`s and `&str`s