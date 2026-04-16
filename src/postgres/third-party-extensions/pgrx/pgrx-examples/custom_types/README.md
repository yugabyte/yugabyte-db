## Custom Types Using `#[derive(PostgresType)]`

`pgrx` is capable of using Rust structs and enums as Postgres data types.  There are two forms.  Rust types
that implement `serde::Serialize` and `serde::Deserialize` are represented as [CBOR](https://crates.io/crates/serde_cbor)
on-disk and as JSON for its text representation.

Rust types that instead can `#[derive(Copy, Clone)]` are represented as a bitwise version of that 
type as a Postgres [`varlena`](https://github.com/postgres/postgres/blob/7559d8ebfa11d98728e816f6b655582ce41150f3/src/include/c.h#L542-L562)
with either a short (1-byte) or a full (4-byte) header, depending on the size_of() the Rust struct.  Such a type's text representation must be implemented manually.

It is also important to note that `PostgresType` can be derived for Rust enums as well if
those enums are not supposed to be
Postgres [enumerated types](https://www.postgresql.org/docs/current/datatype-enum.html),
in which case, `PostgresEnum` should be used instead.

## Custom Types Using `#[derive(PostgresEnum)]`

Rust enums can also be used to declare Postgres [enumerated types](https://www.postgresql.org/docs/current/datatype-enum.html).
These enums' variants must have no associated values.

```rust
#[derive(PostgresEnum, Serialize)]
pub enum SomeValue {
    One,
    Two,
    Three,
    Four,
    Five,
}
```

## serde-compatible Types

Any type that derives (or manually implements) `serde`'s `Serialize` and `Deserialize` traits can be
used as a Postgres type as well.  As mentioned above, `pgrx` will automatically encode/decode values
as CBOR for its on-disk representation, and as JSON for its textual representation.

Additionally, such a type's usage through functions is considered to be an owned value.

```rust
use pgrx::prelude::*;
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PostgresType)]
struct MyType {
    values: Vec<String>,
    thing: Option<Box<MyType>>
}

#[pg_extern]
fn push_value(mut input: MyType, value: String) -> MyType {
    input.values.push(value);
    input
}
```

This would then allow the `push_value()` function to be called from SQL like so:

```sql
SELECT push_value('{"values": ["a", "b", "c"], "thing": null}', 'pgrx');
```

## Copy Types

Any Rust type that is capable of deriving `Copy` and `Clone` are represented bit-by-bit as a binary
blob within a Postgres `varlena` with either a short 1-byte or a full 4-byte header, depending on the 
size of the Rust struct.

These types must implement the text input/output functions themselves, which requires
an additional annotation on the type (`#[pgvarlena_inoutfuncs]`).

When using this type in function arguments and return values, it is required that you use
`PgVarlena<T>`.  This formulation ensures that the datum value from Postgres is properly mapped
into something that looks like your type.

```rust
#[derive(Copy, Clone, PostgresType)]
#[pgvarlena_inoutfuncs]   // This is required for non-serde types
struct MyType {
   a: f32,
   b: f32,
   c: i64
}

/// Implement the PgVarlenaInOutFuncs trait to provide our own text input and output functions
impl PgVarlenaInOutFuncs for MyType {

    // parse the provided CStr into a `PgVarlena<MyType>`
    fn input(input: &core::ffi::CStr) -> PgVarlena<Self> {
        let mut iter = input.to_str().unwrap().split(',');
        let (a, b, c) = (iter.next(), iter.next(), iter.next());

        let mut result = PgVarlena::<MyType>::new();
        result.a = f32::from_str(a.unwrap()).expect("a is not a valid f32");
        result.b = f32::from_str(b.unwrap()).expect("b is not a valid f32");
        result.c = i64::from_str(c.unwrap()).expect("c is not a valid i64");
        result
    }

    // Output ourselves as text into the provided `StringInfo` buffer
    fn output(&self, buffer: &mut StringInfo) {
        buffer.push_str(&format!("{},{},{}", self.a, self.b, self.c));
    }
}

#[pg_extern]
fn do_a_thing(mut input: PgVarlena<MyType>) -> PgVarlena<MyType> {
    input.c += 99;  // performs a copy-on-write on the backing varlena pointer
    input
}
```

## Notes

- For serde-compatible types, you can use the `#[inoutfuncs]` annotation (instead of `#[pgvarlena_inoutfuncs]`) if you'd 
prefer to implement the type's textual representation (maybe JSON isn't what you want).  

    Instead of implementing the trait `PgVarlenaInOutFuncs` (as shown above), you instead implement 
    the trait `InOutFuncs`.  Its `input()` function has a slightly different signature since you'll be 
    creating an owned instance of your type, rather than one mapped by `PgVarlena`.

- Here's a video that walks through some of the example code: https://www.twitch.tv/videos/685570143