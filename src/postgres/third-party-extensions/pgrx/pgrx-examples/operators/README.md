## Postgres Operators with `pgrx`

`pgrx` makes defining custom operators for your custom types very simple.  You can either
manually use the `#[pg_operator]` macro on a function for likely non-standard operators, or you
can use one or more of the the derive macros `#[derive(PostgresEq, PostgresOrd, PostgresHash)]`
if your type is otherwise capable of implementing the equivalent Rust traits.  In that case, `pgrx`
will generate all the standard equality/comparison SQL (and Rust) functions for you.

### Manual Operator Defines

The `#[pg_operator]` macro is used to manually declare a function as a Postgres operator.

Operator functions take one or two arguments and return a non-void value.  The argument types and return types do not necessarily need to be identical.  

If, for example, you have a type, such as:

```rust
#[derive(PostgresType, Serialize, Desearialize)]
struct MyType(String);
```

We can create a "concatenate" operator like so:

```rust
#[pg_operator(immutable, parallel_safe)]
#[opname(||)]
fn mytype_concact(mut left: MyType, right: MyType) -> MyType {
    left.0.push_str(&right.0);
    left
}
```

And now, it'll be usable via SQL as:

```sql
# SELECT '"hello "' || '"world"';
 ?column? 
----------
 "hello world"
(1 row)
```

> note that `#[derive(PostgresType, Serialize, Deserialize)]` uses JSON as the textual representation of a type

You'll notice that we used `#[pg_operator(immutable, parallel_safe)]`.  If those properties are true for your function,
you should specify them.  The defaults are `volatile, parallel_unsafe`.

The `#[opname(||)]` macro specifies the actual SQL operator name.

The complete set of attributes that can be used with `#[pg_operator]`, which correspond
to Postgres' [CREATE OPERATOR](https://www.postgresql.org/docs/12/sql-createoperator.html) 
statement are:

```rust
/* required */
#[opname( <operator symbol name> )]

/* these are optional */
#[negator( <operator symbol name> )]
#[restrict( <function name to use for restrict operator option> )]
#[join( <function name to use for join operator option>)]
#[merges]   // is this operator used for merges of this type?
#[hashes]   // is this operator used for hashes of this type?
```

### Automatically Deriving Operators and Families

`pgrx` also provides three derive macros for automatically implementing the standard Postgres
equality, comparison, and hash operators/functions, along with their `OPERATOR CLASS` and `OPERATOR FAMILY`
definitions.  Using these derive macros will generate the necessary SQL to ensure your
type can be used in `btree` and `hash` indexes.

#### `#[derive(PostgresEq)]`

This derive macro requires that your type also implement Rust's `Eq` and `PartialEq` traits,
either through `#[derive]` or manually.

`pgrx` will then generate `#[pg_operator(immutable, parallel_safe))]`-tagged functions for the
equals (`=`) and not equals (`<>`) operators, properly setting their `#[negator]` attributes.

#### `#[derive(PostgresOrd)]`

This derive macro requires that your type also implement Rust's `Ord` and `PartialOrd` traits,
either through `#[derive]` or manually.

`pgrx` will then generate `#[pg_operator(immutable, parallel_safe)]`-tagged functions for the
following operators:
 - `<`
 - `>`
 - `<=`
 - `>=`
 - a "compare" function named `<typename>_cmp()`

For the generated operators, their `#[negator]` and `#[commutator]` attributes are properly set.

When a type derives both `PostgresEq` and `PostgresOrd`, then `pgrx` also generates the
necessary DDL to define an [OPERATOR CLASS](https://www.postgresql.org/docs/12/sql-createopclass.html)
 and [OPERATOR FAMILY](https://www.postgresql.org/docs/12/sql-createopfamily.html) for the
 type and set of operators.  This allows the type to be used with `btree` indexes.
 
#### `#[derive(PostgresHash)]`
 
This derive macro requires that your type also implement Rust's `Hash` trait, either 
through `#[derive]` or manually.

`pgrx` uses a hasher ([seahash](https://crates.io/crates/seahash)) that's guaranteed 
to be stable.  This is important as Postgres may store a value's hash in a `USING hash`
index.

`pgrx` will then generate a `#[pg_extern(immutable, parallel_safe)]` function named `<typename>_hash()`.

It will also generate an [OPERATOR CLASS](https://www.postgresql.org/docs/12/sql-createopclass.html)
and [OPERATOR FAMILY](https://www.postgresql.org/docs/12/sql-createopfamily.html) for Postgres `hash`
indexes.    
