# v18 One Compile Changelog

This branch changes `pgrx` from a two-artifact schema pipeline into a one-compile pipeline.
The extension shared library is now the source of truth for SQL entity metadata, and the
rest of the branch lines up behind that decision: type identity is explicit, schema graph
resolution is stricter, extension-owned and external SQL types are modeled separately, and
the workspace, examples, tests, and docs were all rebuilt around the new flow.

This document is written as an aggregate changelog for the branch, not as a commit diary.
It describes the current end state after all branch commits have landed.

## Executive Summary

- `cargo pgrx schema` now works from the compiled extension shared library instead of
  generating and running a `pgrx_embed` helper binary.
- New extensions and examples are `cdylib`-only. They no longer need `src/bin/pgrx_embed.rs`,
  `[[bin]]`, or the redundant `"lib"` crate type.
- SQL type resolution now uses `TYPE_IDENT`, a qualified Rust-side identity, instead of the
  older `SCHEMA_KEY` model.
- `SqlTranslatable` now separates "what type is this?" from "how should this appear in SQL?".
- The SQL graph now has clearer rules for external types, extension-owned types,
  `extension_sql!` declarations, explicit composites, and duplicate type producers.
- The repo is now a real workspace with an in-tree `cargo-pgrx`, a slimmed-down `pgrx-tests`
  harness, and a new `pgrx-unit-tests` extension crate that exercises the new model directly.
- The branch also adds the design and review trail that explains why these changes exist:
  RFC 0001, review findings, remediation notes, migration notes, and the v18 type-resolution
  docs.

## One-Pass Schema Generation

The biggest user-visible change is that schema generation is now single-pass.

`cargo pgrx schema` no longer builds a helper executable, runs it, and then rebuilds the
extension. It now does one normal extension build at most, locates the compiled shared
library, reads the embedded SQL entity section from that artifact, deserializes the entities,
and builds SQL or DOT output from there.

That changes a few important details:

- `--skip-build` now means "use the artifact that already exists". If that artifact is stale,
  missing, or incompatible, schema generation fails loudly instead of quietly emitting empty
  or misleading SQL.
- The object reader now lives in `cargo-pgrx` and understands the formats it has to inspect,
  including Mach-O universal binaries. This is no longer outsourced to a helper binary.
- Missing embedded schema metadata is a hard error. The tool now treats that as a broken build
  state, not a recoverable "best effort" path.
- Installed artifacts retain the embedded schema section. The branch ended in a state where
  schema metadata is treated as inert runtime data that should stay attached to the shared
  object, not something `cargo-pgrx` strips away after generation.
  That does make unstripped artifacts a little larger, by the size of the embedded metadata
  payload plus normal section-alignment overhead. On this branch that tradeoff is intentional:
  the freshly built shared object, the installed artifact, and the packaged artifact all keep
  the same schema section attached.
- Versioned shared-object SQL generation now keys off the extension crate version, so the
  generated SQL stays aligned with the actual versioned library name.

The section names were also shortened across platforms. The canonical names are now `.pgrxsc`
on ELF and PE, and `__DATA,__pgrxsc` on Mach-O. Reads remain backward-compatible with the
older `.pgrx_schema` naming, so newer tools can still consume older artifacts.

## Extension Template And Example Changes

Once schema generation stopped depending on a helper executable, the extension layout could
get simpler.

Newly generated extensions now look like normal `cdylib` extensions:

- no `src/bin/pgrx_embed.rs`
- no `[[bin]]`
- no `crate-type = ["lib", "cdylib"]`

The template now emits `crate-type = ["cdylib"]` and keeps the rest of the manifest focused
on the extension itself.

The examples were updated to match that same model. Across the example tree, the old
`pgrx_embed` binaries are gone and the manifests were normalized to the new crate layout.
That means the examples now serve as current reference material for the one-compile flow,
instead of preserving the old two-step scaffolding.

## Type Identity: SCHEMA_KEY To TYPE_IDENT

The SQL entity graph now resolves concrete Rust types through `TYPE_IDENT`.

This is more than a rename. The old "schema key" concept was replaced with a clearer split
between type identity, type origin, and SQL spelling:

- `TYPE_IDENT` is the canonical Rust-side identity used for graph resolution.
- `TYPE_ORIGIN` says whether the type comes from this extension or from outside it.
- `ARGUMENT_SQL` describes how the type appears in argument position.
- `RETURN_SQL` describes how the type appears in return position.

The identity macro behind this is now qualified with `module_path!()`, so two types with the
same spelled name but different module paths don't collapse onto the same graph key. In other
words, resolution is now keyed by a qualified Rust identity instead of a loosely inferred SQL
name.

For the common "this Rust wrapper maps to an existing SQL type" case, the branch
also adds `impl_sql_translatable!`. That helper lives with `SqlTranslatable`,
is re-exported by `pgrx`, and is available from `pgrx::prelude::*`.

The branch also routed concrete type producers and consumers through that model consistently.
Derived SQL entities, manual `SqlTranslatable` impls, and graph lookups all speak the same
identity language now.

## SQL Shape Is Separate From Graph Resolution

Another major branch theme is that SQL spelling and graph resolution are no longer fused
together.

Function and aggregate metadata now carry both:

- optional resolution metadata, used when a type should participate in graph lookup
- SQL-only shape metadata, used to spell the type correctly in emitted SQL

That split matters because wrapper shape and leaf identity are not the same thing.

Wrappers such as `Option<T>`, `Vec<T>`, `Array<T>`, table iterators, and similar constructs
generally forward the leaf type's identity and origin, while the function metadata still keeps
enough SQL shape to emit the correct argument or return form. Resolution follows the concrete
type producer. SQL emission still knows whether the user declared an array, set return, table
return, nullable wrapper, and so on.

This also cleaned up a lot of dead metadata. The branch removed older paths that tried to carry
too much type-origin or identity information in places where it was either redundant or wrong.

## Explicit Composites Are SQL-Only

The branch makes explicit `composite_type!(...)` handling much more precise.

Explicit composites are now modeled as SQL-only declarations. They are not treated as resolvable
Rust type identities. If a function says `composite_type!("Dog")`, the SQL emitter uses that
literal composite name when generating SQL, and the graph does not try to look up some matching
`TYPE_IDENT` for `"Dog"`.

This fixes a real semantic problem in the old model, where explicit composite spellings were too
easy to drag into resolution paths that should only have applied to concrete Rust-defined types.

The same distinction carries through to arrays of explicit composites. The emitted SQL still
comes out correctly, but these uses do not create graph dependencies on derived `PostgresType`
items unless the code actually referenced such a type by identity elsewhere.

## `extension_sql!` Declared Types Now Behave Like First-Class Producers

The branch tightened how `extension_sql!(..., creates = ...)` declarations participate in the
graph.

Declared `Type(...)` and `Enum(...)` entries now keep their `TYPE_IDENT`, and downstream
functions and aggregates can resolve to those declarations just like they resolve to derived
`PostgresType` or `PostgresEnum` entities.

That gives the graph a clean rule:

- one `TYPE_IDENT` can resolve to exactly one producer
- that producer can be a derived type, a derived enum, or an `extension_sql!` declared type/enum

The graph now treats duplicate producers as an error. If two SQL entities claim the same
`TYPE_IDENT`, schema generation fails instead of guessing which one should win.

This work also removed stale metadata from `extension_sql!` declarations. Declared SQL types
keep the identity they need for graph binding, but they no longer carry a separate `type_origin`
there, and functions do not try to smuggle type identity through SQL declaration metadata.

## External Types vs ThisExtension Types

The graph now has much clearer rules for unresolved references.

If a referenced type is marked `TYPE_ORIGIN = External` and there is no in-graph producer for
it, the graph can synthesize an external placeholder node and continue. That keeps built-in or
externally-owned SQL types usable without forcing `pgrx` to pretend it owns them.

If a referenced type is marked `TYPE_ORIGIN = ThisExtension`, the graph no longer falls back to
that placeholder path. A missing producer is a hard error. This is one of the most important
fail-fast changes in the branch: extension-owned references must actually resolve to something
the extension emits.

The `extension_sql!` validation path was tightened to match. `creates = [Type(T)]` and
`creates = [Enum(T)]` are now only accepted for extension-owned, concretely representable SQL
types. External types, skipped types, explicit composites, arrays, and similar unsupported
shapes are rejected up front instead of entering the graph half-specified.

## SQL Emission And Ordering Rules

The graph and emission passes also got more exact about ordering and dependency semantics.

- Derived `PostgresType` SQL is emitted in shell-first order: create the shell, wire up the I/O
  pieces, then materialize the final type definition.
- Within strongly connected components, explicit `requires = [...]` edges now outrank weaker
  type-resolution edges. That matters for SQL-only composites and shell-type ordering.
- Functions and aggregates only ask the graph for a schema prefix when they have resolution
  metadata for a type. SQL-only composites bypass that lookup and emit their declared composite
  name directly.
- Declared `extension_sql!` types participate in dependency ordering like real producers, so
  downstream items can depend on those declarations instead of only on derived Rust entities.

The result is a graph that is both stricter and easier to reason about. More cases are modeled
directly, and fewer cases rely on accidental behavior from over-general metadata.

## Test Harness, Workspace, And CI Rework

This branch also rebuilt the repo layout around the new pipeline.

The root is now an actual workspace that includes:

- `cargo-pgrx`
- the core `pgrx` crates
- `pgrx-tests`
- `pgrx-unit-tests`
- `pgrx-examples/*`

The workspace metadata also points local installs at the in-tree `cargo-pgrx`, which lets the
test harness and CI exercise the branch's `cargo-pgrx` implementation instead of whatever
binary happens to be installed globally.

The test story was split cleanly:

- `pgrx-tests` is now the reusable harness crate
- `pgrx-unit-tests` is the internal test extension crate

`pgrx-tests` now defaults to that smaller harness surface instead of trying to double as the
internal test extension itself.

That split matters because the old setup blurred "helper library" and "thing being tested".
The current layout makes it much easier to run the internal extension against the same schema
generation and graph code that real users hit.

Coverage was expanded around the new behavior:

- `TYPE_IDENT` and `SqlTranslatable` coverage grew substantially
- compile-fail tests now check that manual `SqlTranslatable` impls define `TYPE_IDENT`
- compile-fail tests also verify that wrappers don't hide unsupported non-SQL leaf types
- CI now runs `cargo test --all` across PostgreSQL 13 through 18
- CI includes a beta Rust leg on PostgreSQL 16
- CI also checks `pgrx-unit-tests` in both `cshim` and non-`cshim` modes, runs the UI tests,
  performs an arm64 cross-build check, smoke-tests `cargo pgrx bench`, and exercises schema
  generation against the versioned custom-libname example

This is one of the branch's quieter but more important outcomes. The new invariants are not
just documented. They are exercised in-tree.

## Documentation And Review Trail

This branch carries its own design and review history in the tree.

New or significantly expanded docs in the branch include:

- `rfcs/0001-single-pass-schema-generation.md`
- `rfcs/0001-initial-deep-review.md`
- `rfcs/0001-review-findings.md`
- `rfcs/0001-deep-review-findings.md`
- `rfcs/0002-review-remediation.md`
- `v18.0-MIGRATION.md`
- `v18-TYPE-RESOLUTION.md`

The internal documentation was also updated to describe the linker-section-based schema flow,
and the older "forging SQL from Rust" article was left in place as historical material rather
than current implementation guidance.

That matters for this branch because a lot of the changes here are not obvious from a surface
API diff. The RFC and review trail records the failure cases that motivated the stricter graph
rules and the one-pass artifact design.

## Smaller Cleanup That Still Matters

Not every commit in the branch introduced a new concept, but several follow-up commits materially
improved the final state:

- a lifetime cleanup replaced a broad set of `Box::leak` patterns in schema and SQL graph code
  with borrowed data where possible
- a code-review follow-up trimmed a few rough edges in the object reader, docs, SQL emission,
  and graph code
- tuple iterator naming warnings were silenced in `pgrx`
- repo-local ignore rules were updated for the current working setup
- the final doc cleanup pass tightened wording in the new v18 docs

These are small compared to the core architecture work, but they are part of the branch's final
shape and should be called out as such.

## Practical Migration Notes

For extension authors, the branch leaves a few clear takeaways:

- delete old `pgrx_embed` binaries and `[[bin]]` entries
- use `crate-type = ["cdylib"]`
- treat missing embedded schema metadata as a build problem, not something `cargo pgrx schema`
  should paper over
- on manual `SqlTranslatable` impls, define `TYPE_IDENT`
- for fixed external SQL mappings, prefer `impl_sql_translatable!(T, "...")`
- don't rely on wrappers to make a non-SQL leaf type acceptable
- use `extension_sql!(..., creates = ...)` only for extension-owned, concrete SQL types or enums
- expect explicit `composite_type!(...)` declarations to stay SQL-only unless there is a real
  concrete type producer elsewhere in the graph

In short, the branch trades some old flexibility for a model that is much more explicit. That is
intentional. The current code prefers exact identity, exact ownership, and hard failures over
implicit guesses.

## Commit Coverage

This changelog covers the branch's current post-rebase commits in aggregate:

- design, review, and doc trail: 6b7c1ff9, 226f7f9a, bd7e04d4, 6cdb7eb1, 074a3b1d
- one-pass schema pipeline and artifact handling: e317229b, 62d16338, 2c39c1f4, bd093d6b
- template and extension layout simplification: 7067708c
- type identity and graph semantics: ab9d9a49, 575322c7, 116da7f0, ff140f50, d3eab0f5,
  d023f577, 1d1ff15e, 30aec2e2, d8b55c7e, 9f665b07
- workspace, harness, and test expansion: a5829b05, 194e986c, b4750d9a, 57fc49cd
- follow-up cleanup and polish: dc6aee0b, e5692a63, 1e2fadab, db180e14
- changelog addition: e1ab0658

If you want the shortest possible summary of the branch, it is this: we now compile the
extension once, read its schema metadata from the shared object, resolve SQL types by explicit
qualified identity, and fail hard when the graph can't prove what owns a type.
