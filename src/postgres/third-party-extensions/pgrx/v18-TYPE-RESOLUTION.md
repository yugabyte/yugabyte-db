# v18 Type Resolution

This document explains how v18 gets from whatever Rust type syntax the user
wrote to two different outputs:

- `TYPE_IDENT`, the identity used for schema-graph resolution
- SQL spelling, the text that ends up in generated SQL such as `uuid`,
  `complex`, or `complex[]`

The main thing to keep in your head is this:

- `TYPE_IDENT` answers "what Rust-defined thing is this?"
- `ARGUMENT_SQL` and `RETURN_SQL` answer "what SQL text should we emit?"

Those are related, but they are not the same thing.

## Why This Exists

v18 moved schema generation to a single compilation pass.

That means type metadata has to be available during the normal build, in a form
that macros can emit into the shared object. Later, `cargo pgrx schema` reads
that metadata back out of the compiled artifact and turns it into SQL.

So the pipeline now looks like this:

```text
Rust source tokens
    |
    v
macro parsing and type normalization
    |
    v
pick <T as SqlTranslatable>
    |
    +--> TYPE_IDENT + TYPE_ORIGIN
    |
    `--> ARGUMENT_SQL + RETURN_SQL
    |
    v
embed binary metadata into .pgrxsc / __DATA,__pgrxsc
    |
    v
cargo pgrx schema decodes entities
    |
    v
PgrxSql resolves TYPE_IDENT to a graph target
    |
    v
SQL emitter uses ARGUMENT_SQL / RETURN_SQL
and adds schema prefixes from the resolved target
```

## The Four Different "Names"

There are really four different layers in play:

| Layer                | Example                                         | What it is used for                                                          |
|----------------------|-------------------------------------------------|------------------------------------------------------------------------------|
| Rust source tokens   | `Vec<Option<MyType>>`                           | What the user wrote                                                          |
| Normalized Rust type | `Vec<Option<MyType>>` with lifetimes anonymized | Picking the right `SqlTranslatable` impl and preserving readable diagnostics |
| `TYPE_IDENT`         | `my_extension::MyType`                          | Matching Rust references to SQL-owning entities in the graph                 |
| SQL spelling         | `my_type[]`                                     | The actual SQL text emitted for an argument or return type                   |

Two consequences fall out of this:

1. We do not try to infer SQL names directly from arbitrary source tokens.
2. We do not use SQL spelling to decide graph identity.

The source syntax is only the path to a Rust type. After that, `SqlTranslatable`
is the source of truth.

## Surface Spelling Is Not The Lookup Key

This is worth calling out early because it explains a lot of the rest of the
document.

pgrx does not resolve types by comparing whatever token text the user wrote in
source.

It parses the type, normalizes it, and then emits code that asks Rust for the
`SqlTranslatable` impl of the resolved type.

In practice, that means macro expansion ends up doing lookups like:

```rust
<#resolved_ty>::entity()
```

and:

```rust
<#resolved_ty as SqlTranslatable>::TYPE_IDENT
```

That is the real engine here.

So these source spellings may look very different:

```rust
UuidWrapper
crate::path::to::UuidWrapper
Vec<UuidWrapper>
std::vec::Vec<crate::path::to::UuidWrapper>
```

But if Rust resolves them to the same underlying types, pgrx follows the same
trait impls and gets the same metadata out the other end.

That is why the docs often show short spellings for readability. The short
spelling is not special. It is just easier to look at.

## Step 1: Parse And Normalize The Rust Type

When `#[pg_extern]`, aggregates, triggers, and similar macros look at a type,
they do not keep the raw tokens untouched.

They first build a `UsedType`, which normalizes the syntax into something pgrx
can reason about.

This normalization does a few important things:

- peels `default!(...)`
- resolves `variadic!()` and `VariadicArray<T>`
- resolves `composite_type!(...)`
- inspects container and wrapper shapes such as `Option<T>`, `Result<T, E>`,
  `Vec<T>`, `Array<T>`, and friends, and records flags such as `optional` and
  `variadic`
- rewrites nested `composite_type!(...)` cases just enough to preserve the
  wrapper shape around the synthetic composite Rust type
- anonymizes lifetime names before serializing metadata

That last part matters. Local lifetime spellings like `'a` and `'mcx` are not
meant to create different schema identities just because the caller chose a
different letter.

What survives this stage is:

- a normalized Rust type, usually still including the wrapper shape, that can
  be used in `<T as SqlTranslatable>`
- flags such as `optional`, `variadic`, and `default`
- sometimes an explicit composite SQL name

## Step 2: `SqlTranslatable` Defines The Boundary

Once pgrx has a normalized Rust type, it stops reasoning from syntax and starts
reasoning from trait metadata.

The boundary is:

```rust
unsafe trait SqlTranslatable {
    const TYPE_IDENT: &'static str;
    const TYPE_ORIGIN: TypeOrigin;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError>;
    const RETURN_SQL: Result<ReturnsRef, ReturnsError>;
}
```

This is the actual contract.

For the common "fixed external SQL type" case, you usually don't need to write
all four consts by hand. `impl_sql_translatable!(T, "uuid")`, re-exported by
`pgrx::prelude::*`, expands to the same contract with
`TYPE_IDENT = pgrx_resolved_type!(T)`, `TYPE_ORIGIN = TypeOrigin::External`,
and matching argument and return SQL. The `arg_only = "..."` form keeps the
same external identity but leaves the return mapping invalid.

### `TYPE_IDENT`

`TYPE_IDENT` is the graph identity for the Rust type.

For extension-defined types, the intended spelling is:

```rust
const TYPE_IDENT: &'static str = pgrx::pgrx_resolved_type!(MyType);
```

Today, `pgrx_resolved_type!(T)` expands to:

```rust
concat!(module_path!(), "::", stringify!(T))
```

That means the identity comes from the module where the impl or derive expands,
plus the type tokens you pass to the macro.

For derive-generated impls, and for manual impls written next to the type, that
lines up with the type's canonical module path rather than whatever alias,
re-export, or call-site spelling showed up somewhere else.

This is why a type alias and the original type usually share the same
`TYPE_IDENT` when they resolve to the same `SqlTranslatable` impl.

### `TYPE_ORIGIN`

`TYPE_ORIGIN` says where the SQL type is expected to come from:

- `TypeOrigin::ThisExtension`: the graph must find a matching type, enum, or
  `extension_sql!(..., creates = [...])` declaration in this extension
- `TypeOrigin::External`: the graph may treat it as an already-existing SQL
  type and create an external placeholder if needed

### `ARGUMENT_SQL` and `RETURN_SQL`

These are the actual emitted SQL spellings.

Examples:

- `Ok(SqlMappingRef::literal("uuid"))`
- `Ok(ReturnsRef::One(SqlMappingRef::literal("complex")))`
- array mappings such as `text[]`
- composite mappings
- `Skip`, for types that should not appear in emitted SQL

This is where SQL text comes from. Not from `TYPE_IDENT`.

## Step 3: Wrapper Types Often Keep Identity But Change SQL

One of the easiest mistakes is assuming wrapper types must always get their own
`TYPE_IDENT`. They often do not.

Many wrapper impls forward identity from the inner type:

- `Option<T>` keeps `T::TYPE_IDENT`
- `Result<T, E>` keeps `T::TYPE_IDENT`
- `*mut T` keeps `T::TYPE_IDENT`
- `Array<T>` and `VariadicArray<T>` keep `T::TYPE_IDENT`
- `Vec<T>` keeps `T::TYPE_IDENT`

But the SQL spelling may still change.

For example, `Vec<T>` keeps the same identity as `T`, while turning the SQL
mapping into an array form. `Vec<u8>` is the special case that maps to `bytea`.

Again, this works because the generated code does not ask "did the user write
the short name `Vec<T>`?" It asks Rust for the `SqlTranslatable` impl of the
resolved wrapper type, and the `Vec<T>` impl forwards identity from `T`.

So this is a perfectly normal outcome:

| Rust type        | `TYPE_IDENT`           | SQL spelling |
|------------------|------------------------|--------------|
| `MyType`         | `my_extension::MyType` | `my_type`    |
| `Option<MyType>` | `my_extension::MyType` | `my_type`    |
| `Vec<MyType>`    | `my_extension::MyType` | `my_type[]`  |

Identity and spelling move independently.

## Step 4: Some Types Intentionally Skip Identity Resolution

`composite_type!(...)` is the main special case.

If a function argument or return type uses an explicit composite SQL name, pgrx
stores that as SQL-only metadata. It does not emit a `(TYPE_IDENT, TYPE_ORIGIN)`
pair for that slot.

In other words, this:

```rust
#[pg_extern]
fn takes_dog(dog: pgrx::composite_type!("Dog")) -> pgrx::composite_type!("Dog") {
    todo!()
}
```

does not ask the graph to resolve some Rust type identity named `Dog`.

It says: use the composite SQL name `Dog` directly.

That is why explicit composite mappings do not participate in type-ident
matching.

## Step 5: Derives And `extension_sql!` Create Graph Targets

`TYPE_IDENT` only becomes useful if something in the graph can own it.

There are three main ways that happens.

### 1. `#[derive(PostgresType)]`

The derive generates:

- a `SqlTranslatable` impl with `TYPE_IDENT = pgrx_resolved_type!(T)`
- SQL spelling from the derived type name
- a `PostgresTypeEntity` carrying the same `TYPE_IDENT`

So the graph has both:

- a place that refers to the type
- a place that owns the type

### 2. `#[derive(PostgresEnum)]`

Same idea as `PostgresType`, but for enum entities.

### 3. `extension_sql!(..., creates = [Type(T)]/[Enum(T)])`

This is the manual path for extension-owned SQL declarations.

When you write:

```rust
extension_sql!(
    r#"CREATE TYPE complex;"#,
    name = "create_complex_shell_type",
    creates = [Type(Complex)]
);
```

pgrx records two things:

- the concrete SQL spelling, taken from `<Complex as SqlTranslatable>::ARGUMENT_SQL`
- the owning `TYPE_IDENT`, taken from `<Complex as SqlTranslatable>::TYPE_IDENT`

That is what lets a later `#[pg_extern] fn f(x: Complex)` resolve to the SQL
type created by that `extension_sql!()` block.

## Step 6: Emit Metadata Into The Shared Object

Each macro expansion emits a compact binary entry into the schema linker
section:

- `.pgrxsc` on ELF and PE
- `__DATA,__pgrxsc` on Mach-O

The entries are emitted as `static` byte arrays. There is no second helper
binary anymore.

For type-using slots, pgrx serializes:

- an optional `(TYPE_IDENT, TYPE_ORIGIN)` resolution tuple
- the argument SQL mapping
- the return SQL mapping

For example, a function slot that needs graph resolution will carry:

```text
some(type_ident = "my_extension::Complex", type_origin = ThisExtension)
+ argument_sql = "complex"
+ return_sql = "complex"
```

An explicit composite slot will carry:

```text
no type resolution
+ composite_type = "Dog"
+ argument_sql = composite
+ return_sql = composite
```

## Step 7: `cargo pgrx schema` Reads The Section Back

Later, `cargo pgrx schema`:

1. builds the extension shared object
2. reads the embedded schema section from the compiled artifact
3. decodes the binary entries into `SqlGraphEntity` values
4. builds a dependency graph
5. emits ordered SQL

This is still single-pass schema generation because there is only one compile
of the extension itself. The later schema step is reading metadata, not
recompiling the extension to discover it.

## Step 8: Graph Resolution Uses `TYPE_IDENT`, Not SQL Spelling

This is the resolution rule in plain English:

```text
if TYPE_IDENT matches a type, enum, or declared creates=[...] target:
    use that graph node
else if TYPE_ORIGIN is External:
    create or reuse an external placeholder
else:
    error
```

A few important properties come out of this.

### Duplicate owners are rejected

If the same `TYPE_IDENT` is claimed by more than one graph target, schema
generation fails.

That includes clashes across:

- derived types
- derived enums
- `extension_sql!(..., creates = [...])`

### `ThisExtension` must resolve locally

If a slot says:

```rust
const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
```

then pgrx expects to find a matching type owner in the extension graph.

If it cannot, schema generation fails with an unresolved type-ident error.

### `External` allows placeholders

If a slot says:

```rust
const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
```

then pgrx may create an external placeholder node instead of requiring a local
owner.

That is how manual wrappers over built-in or pre-existing SQL types work.

## Step 9: SQL Emission Uses SQL Mappings Plus Graph Prefixes

Once the graph is built, SQL emission does two separate jobs:

1. choose the SQL spelling from `ARGUMENT_SQL` or `RETURN_SQL`
2. decide whether a schema prefix is needed by following the resolved graph
   dependency

That means:

- `TYPE_IDENT` decides which graph node a slot points at
- `ARGUMENT_SQL` and `RETURN_SQL` decide the printed type text

This is the piece that most often gets blurred together.

If a slot resolves to an extension-owned type in some schema, pgrx can prefix
the emitted SQL type with that schema.

If a slot is external, pgrx emits the external SQL spelling directly.

## Worked Examples

### Example 1: Extension-owned manual type

```rust
use pgrx::pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable,
    TypeOrigin,
};

pub struct Complex;

extension_sql!(
    r#"CREATE TYPE complex;"#,
    name = "create_complex_shell_type",
    creates = [Type(Complex)]
);

unsafe impl SqlTranslatable for Complex {
    const TYPE_IDENT: &'static str = pgrx::pgrx_resolved_type!(Complex);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::ThisExtension;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("complex"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("complex")));
}

#[pg_extern]
fn echo_complex(value: Complex) -> Complex {
    value
}
```

What happens:

- the function signature contains the Rust token `Complex`
- macro normalization selects `<Complex as SqlTranslatable>`
- `TYPE_IDENT` becomes something like `my_extension::Complex`
- `TYPE_ORIGIN` says the owner must be inside this extension
- `creates = [Type(Complex)]` registers an owning graph target for the same
  `TYPE_IDENT`
- SQL spelling comes from `"complex"`

The important part is that the graph match happens on `my_extension::Complex`,
while the emitted SQL text is `complex`.

### Example 2: Manual wrapper for an existing SQL type

```rust
use pgrx::prelude::*;

pub struct UuidWrapper;

impl_sql_translatable!(UuidWrapper, "uuid");

#[pg_extern]
fn echo_uuid(value: UuidWrapper) -> UuidWrapper {
    value
}
```

What happens:

- the macro sets the Rust identity to `my_extension::UuidWrapper`
- there is no requirement for a local type owner because the origin is
  `External`
- the emitted SQL type is `uuid`
- no `CREATE TYPE uuid_wrapper` statement appears just because the trait exists

This is the clean example of why `TYPE_IDENT` and SQL spelling must stay
separate.

### Example 3: Array wrappers keep the same identity

```rust
#[pg_extern]
fn echo_many_short(values: Vec<UuidWrapper>) -> Vec<UuidWrapper> {
    values
}

#[pg_extern]
fn echo_many_qualified(
    values: std::vec::Vec<crate::path::to::UuidWrapper>,
) -> std::vec::Vec<crate::path::to::UuidWrapper> {
    values
}
```

These two signatures are equivalent for type resolution.

What happens:

- the source syntax may be the short `Vec<UuidWrapper>` spelling or the fully-qualified
  `std::vec::Vec<crate::path::to::UuidWrapper>` spelling
- macro normalization still identifies the outer wrapper as `Vec`
- the generated code asks Rust for the `SqlTranslatable` impl of the resolved
  wrapper type
- the selected impl is still `SqlTranslatable for Vec<T>`
- `TYPE_IDENT` is still `UuidWrapper::TYPE_IDENT`
- SQL spelling becomes `uuid[]`

So pgrx treats the slot as "the same logical type identity, but array-shaped in
SQL".

### Example 4: Explicit composite SQL bypasses type identity

```rust
#[pg_extern]
fn rename_dog(dog: pgrx::composite_type!("Dog")) -> pgrx::composite_type!("Dog") {
    todo!()
}
```

What happens:

- the source syntax contains `composite_type!("Dog")`
- normalization records the explicit composite SQL name
- function metadata for that slot is SQL-only
- no `TYPE_IDENT` lookup happens for the argument or return slot

This is intentionally different from the derived and manual `SqlTranslatable`
paths.

## A Useful Mental Model

If you remember one thing, remember this split:

```text
Rust syntax        -> choose the Rust type and wrapper shape
TYPE_IDENT         -> find the owning graph node
ARGUMENT_SQL /
RETURN_SQL         -> print the SQL type text
TYPE_ORIGIN        -> say whether the owner must be local or may be external
```

Or, even shorter:

```text
identity is for matching
SQL mapping is for printing
origin is for resolution policy
```

That is the whole v18 type-resolution model.
