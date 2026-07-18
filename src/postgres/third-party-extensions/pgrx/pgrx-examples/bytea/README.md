Examples for working with Postgres `bytea` type.

Postgres' `bytea` type can be represented as a borrowed `&[u8]` or an owned `Vec<u8>` with Rust.

This example demonstrates how to use [`libflate`](https://crates.io/crates/libflate) to gzip/gunzip `bytea` (and `text`) data directly from UDFs.