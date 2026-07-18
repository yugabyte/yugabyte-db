# PGRX Internals

There are three major components to pgrx:

- [runtime libraries for extensions](#runtime-libraries)
- [supporting build tools](#build-tools)
- [correctness checks via the test suite](#test-suite)

Though given that one component includes a pile of macros, and another component involves
a rather sophisticated runtime element itself, the distinction often blurs.

# runtime libraries

## pgrx

The main library that offers safe abstractions that wrap pgrx internals.

### About macros and pgrx modules

Currently, PGRX (the collection of libraries and tools) only assumes that this crate is used as
the primary interface. Most pgrx code is generated via macros, which themselves may get used in
odd situations. It is thus somewhat important to qualify most code that is introduced via macros
by using `::pgrx` along with some module, so that the actual top-level crate named pgrx is used.

Thus almost every library crate is named `pgrx-{module}` and also introduced as `pgrx::{module}`.

### pgrx::prelude

Previously, the recommended way to use pgrx was to glob-import everything. However, this has
a problem in that pgrx has over time evolved to have use-cases which do not involve exposing
all the `unsafe fn` of pgrx and its extensional innards to the "end-user".
In other words, PL/Rust was born. Thus the prelude was introduced to limit our concerns.

The prelude's contents should thus focus mostly on safe code and common types.

### Designing new abstractions

New abstractions in pgrx should avoid assuming that they are the only interface to Postgres, as
pgrx-based extensions are often used in combination with C-based extensions, or are backed by
considerable custom FFI written by a programmer.

## pgrx-pg-sys

This crate comes with an extensive `build.rs` to generate bindings for every Postgres version.

## pgrx-macros

...whether this is a runtime library or a build tool is a philosophical argument.

# build tools

## pgrx-sql-entity-graph

This crate may seem... somewhat distressing. It is a rather lot of code, some of it without
obvious purpose. One may develop fantasies about deleting it wholesale.  Unfortunately, it serves
an irreplaceable functionality in pgrx, even if its role could be reduced.

A primary task for pgrx, accomplished via cargo-pgrx and support libraries, is to generate SQL.
This allows programmers to avoid separately maintaining the SQL that installs the extension when
`CREATE EXTENSION` runs. This is not merely a convenience: misdeclaring extensions, functions,
types, and operators can in some cases cause illogical behavior from Postgres.

For instance, Postgres [optimizes queries based on `CREATE OPERATOR` DDL][operator-optimization].
This means that an incorrect declaration of an operator can return invalid query results.
An extension can even interact with PostgreSQL in more complicated ways that mean these types of
assumptions could corrupts a table's active rows or index. Postgres employs various techniques to
detect such incorrect behaviors, such that it will abort transactions and roll back if it does,
and this may not be "undefined behavior" in Rust terms, but it is still undesirable!

To limit this sort of problem, Postgres will reject known-invalid declarations of CREATE TYPE,
CREATE FUNCTION, and so on. This imposes an ordering constraint, however, as "invalid" includes
"referencing types or functions that do not yet exist", and Postgres has relatively few ways to
issue any kind of "forward declaration". Thus, while handwritten SQL might tend to be ordered in
ways that naturally satisfy this ordering, automated SQL generation technique requires sorting the
SQL declarations to meet these constraints.

Currently, this crate *also* performs the task of reasoning about various lifetime issues in Rust,
defining interfaces for translating Rust types to-and-from SQL, and some Rust code generation.
It may be possible to separate this from code that maintains aforementioned sorting information,
at least somewhat. Or not.

## cargo-pgrx

Together with `pgrx-sql-entity-graph`, this implements a rather astounding hack:

Various functions are injected into the Rust library, which are then dlopened and called to
extract the required SQL!

See [Forging SQL from Rust](../articles/forging-sql-from-rust.md) for more.

## pgrx-pg-config

This is effectively support library for the build.rs of pgrx-pg-sys and for cargo-pgrx.

## pgrx-version-updater

This tool just helps us update versions for various toml files in the repo, because
the alternative is a horrible pile of bash and regexen.

# test suite

This should be run in CI for every merge.

## pgrx-tests
This currently contains both our test support framework and the actual test suite.

Fortunately, the way `#[pg_test]` works is magic enough to simply happen if you run `cargo test`.
Unfortunately, due to the way that `#[pg_test]` works, the placement of test code is extremely
constrained in terms of where it must be in files. This is part of why we have this
additional crate.

## pgrx-examples
Various example extensions one can define using pgrx.

New features that introduce a noteworthy "kind" of extension or a feature for extensions not
defined by the libraries per se should probably have an example added for them here.


<!-- Links -->

[operator-optimization]: https://www.postgresql.org/docs/current/xoper-optimization.html
