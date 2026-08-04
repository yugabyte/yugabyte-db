## Schemas and Rust Modules

A `pgrx`-based extension is created in the schema determined by the `CREATE EXTENSION` statement.
If unspecified, that schema is whatever the first schema in the user's `search_path` is, otherwise
it is the schema argument to `CREATE EXTENSION`.

In general, any `pgrx` object (a function, operator, type, etc), regardless of the Rust source
file it is defined in, is created in that schema unless that object appears in a
`#[pg_schema] mod modname { ... }` block.  In this case, `pgrx` generates a top-level schema named the
same as the module, and creates the contained objects within that schema.

Unlike Rust, which supports nested modules, Postgres only supports one-level of schemas,
although a Postgres session can have many schemas in its `search_path`.  As such, any
`#[pg_schema] mod modname { ... }` block containing `pgrx` objects is hoisted to a top-level schema.

### `#[pg_extern]/#[pg_operator]` Functions and their Postgres `search_path`

When `pgrx` generates the DDL for a function (`CREATE FUNCTION ...`), it can apply `search_path` to
that function that limits that function's search_path to whatever you specify.
This is done via the `#[search_path(...)]` attribute macro applied to the function
with `#[pg_extern]` or `#[pg_operator]` or `#[pg_test]`.

For example:

```rust
#[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
pub struct SomeStruct {}

#[pg_extern]
#[search_path(@extschema@)]
fn return_vec_of_customtype() -> Vec<SomeStruct> {
    vec![SomeStruct {}]
}
```

`@extschema@` is likely all you'd ever want.  It's a token that Postgres itself will substitute during `CREATE EXTENSION`
to be that of the schema in which the extension is being installed.

You can, however instead use whatever search_path you'd like:

```rust
#[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
pub struct SomeStruct {}

#[pg_extern]
#[search_path(schema_a, schema_b, public, $user)]
fn return_vec_of_customtype() -> Vec<SomeStruct> {
    vec![SomeStruct {}]
}
```

In general this is only necessary when returning a `Vec<T: PostgresType>`.  In this situation, pgrx needs to know that type's
`oid` (from the `pg_catalog.pg_type` system catalog) and as such, the schema in which that type lives must be on that
function's `search_path`.

### Relocatable extensions

Previously, PGRX would schema-qualify all of the output of `cargo pgrx schema` if there was a `schema = foo` attribute in your
extension `.control` file, including items outside of a `#[pg_schema]` macro. However, this meant that pgrx could not support [relocatable extensions](https://www.postgresql.org/docs/current/extend-extensions.html#EXTEND-EXTENSIONS-RELOCATION).
This is because relocatable extensions' defining characteristic is that they can be moved from one schema to another, and that
means you absolutely cannot statically determine the schema the extension lives in by reading the control file alone.
Since this change was implemented, you can now set `relocatable = true` in your control file without issue, and relocatable
extensions can be moved with a `ALTER EXTENSION your_extension SET SCHEMA new_schema;` query.
