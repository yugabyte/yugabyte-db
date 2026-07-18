# Writing Extensions with PGRX

<!-- TODO: write more of this -->

If you use `cargo pgrx new "${extension_name}"`, you get a new directory set up with that name,
a Cargo.toml, a .control file, and some templated Rust for that extension.

<details><summary><strong>About the Templated Code</strong></summary>

This templated code is pregenerated, so you don't need to worry about it.
However, if you are curious as to why it has to look a specific way:

Any pgrx extension will require you to add `pgrx` to your Cargo.toml and add this in the `lib.rs`:

```rust
pgrx::pg_module_magic!();
```

This generates a few functions that Postgres will call and use to identify your loaded extension.

</details>

When you write a function that will be exposed to Postgres, there are two ABIs to worry about:
the Postgres ABI and the C ABI. The Postgres ABI is built on top of the C ABI, so all Postgres
ABI functions are also C ABI functions, but not all C ABI functions that Postgres will call
use the Postgres ABI. The Postgres ABI is only for functions that will be called via the
"function manager", and are thus exposed in some way to SQL.

The Postgres ABI is handled by adding `#[pg_extern]` to a function.

A C ABI function that may be called as a callback by Postgres requires `#[pg_guard]` attached.
This is due to Postgres using [setjmp and longjmp](../pg-internal/setjmp-longjmp.md) for a
form of "exception-handling". This interacts poorly with Rust panic-handling unless translated.
