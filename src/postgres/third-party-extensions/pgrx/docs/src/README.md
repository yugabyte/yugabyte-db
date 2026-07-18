# Introduction to PGRX

The core idea of pgrx is that writing Postgres extensions with `pgxs.mk` requires
- writing C code that correctly binds Postgres headers and is a loadable dynamic library
- writing many SQL [CREATE FUNCTION] and [CREATE TYPE] declarations
- correctly handling all C language invariants
- correctly handling all SQL invariants, since SQL for extensions can be `unsafe`
- correctly handling all Postgres-specific invariants, like the function argument ABI

This demands expert-level knowledge of C, SQL, and the inner workings of Postgres,
just to write an extension. All of this is before the programmer can even get to write
the actual extension's code, which will often require its own specialized domain knowledge!

Alternatively, with Rust, safe abstractions can be designed to encode the
invariants that Postgres requires in types. Powerful procedural macros can
generate the code to handle the Postgres function ABI, or even write the
needed SQL declarations! This is what pgrx does, with the intent to allow
writing extensions correctly while only being familiar with a single language:
Rust.

...and pgrx. While the annotations that pgrx requires are easy to write, they
aren't necessarily automatic. And even if it is simpler than `pgxs.mk`, the
pgrx build system, primarily wielded through `cargo pgrx`, sometimes needs user
intervention to fix problems. Most of this is in service of allowing you to
adjust exactly how much pgrx assists you, so that it doesn't get in the way
if you need to force something inside it.

<!-- the following may currently be aspirational rather than actual -->
This guide assumes the reader (you) are a Rust programmer, but it does not
expect you to be intimately familiar with Postgres, nor deeply nuanced in FFI.
It may assume familiarity with C and SQL work, but as long as you have written
`extern "C"` or `LEFT JOIN` before, you should be fine.

[CREATE FUNCTION]: https://www.postgresql.org/docs/current/sql-createfunction.html
[CREATE TYPE]: https://www.postgresql.org/docs/current/sql-createtype.html

## Table of Contents
- [Working with PGRX](./extension/README.md)
- [Basics of Postgres Internals](./pg-internal.md)
- [Contributing](./contributing.md)
- [Articles](./articles.md)
- [Design Decisions](./design-decisions.md)