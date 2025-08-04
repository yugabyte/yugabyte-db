# PostgreSQL Aggregates with Rust
<!-- Written 2022-02-09 -->

Reaching for something like `SUM(vals)` or `AVG(vals)` is a common habit when using PostgreSQL. These [aggregate functions][postgresql-aggregate-functions] offer users an easy, efficient way to compute results from a set of inputs.

How do they work? What makes them different than a function? How do we make one? What kinds of other uses exist?

We'll explore creating some basic ones using SQL, then create an extension that defines aggregates in Rust using [`pgrx`][pgrx] 0.3.0's new aggregate support.

<!-- more -->

# Aggregates in the world

Aggregates have a number of uses, beyond the ones already mentioned (sum and average) we can find slightly less conceptually straightforward aggregates like:

* JSON/JSONB/XML 'collectors': [`json_agg`][postgresql-aggregate-functions], [`json_object_agg`][postgresql-aggregate-functions], [`xmlagg`][postgresql-aggregate-functions].
* Bitwise operators: [`bit_and`][postgresql-aggregate-functions], [`bit_xor`][postgresql-aggregate-functions], etc.
* Some [`std::iter::Iterator`][std::iter::Iterator] equivalents:
    + [`all`][std::iter::Iterator::all]: [`bool_and`][postgresql-aggregate-functions]
    + [`any`][std::iter::Iterator::any]: [`bool_or`][postgresql-aggregate-functions]
    + [`collect::<Vec<_>>`][std::iter::Iterator::collect]: [`array_agg`][postgresql-aggregate-functions]
* [PostGIS][postgis]'s geospacial [aggregates][postgis-aggregates] such as [`ST_3DExtent`][postgis-aggregates-3d-extent] which produces bounding boxes over geometry sets.
* [PG-Strom][pg-strom]'s [HyperLogLog aggregates][pg-strom-aggregates] such as [`hll_count`][pg-strom-aggregates].

So, aggregates can collect items, work like a [`fold`][std::iter::Iterator::fold], or do complex analytical math... how do we make one?

# What makes an Aggregate?

Defining an aggregate is via [`CREATE AGGREGATE`][postgresql-create-aggregate] and [`CREATE FUNCTION`][postgresql-create-function], here's a reimplementation of `sum` only for `integer`:

```sql
CREATE FUNCTION example_sum_state(
    state integer,
    next  integer
) RETURNS integer
LANGUAGE SQL
STRICT
AS $$
    SELECT $1 + $2;
$$;

CREATE AGGREGATE example_sum(integer)
(
    SFUNC    = example_sum_state, -- State function
    STYPE    = integer,           -- State type
    INITCOND = '0'                -- Must be a string or null
);

SELECT example_sum(value) FROM UNNEST(ARRAY [1, 2, 3]) as value;
--  example_sum 
-- -------------
--            6
-- (1 row)
```

Conceptually, the aggregate loops over each item in the input and runs the `SFUNC` function on the current state as well as each value. That code is analogous to:

```rust
fn example_sum(values: Vec<isize>) -> isize {
    let mut sum = 0;
    for value in values {
        sum += value;
    }
    sum
}
```

Aggregates are more than just a loop, though. If the aggregate specifies a `combinefunc` PostgreSQL can run different instances of the aggregate over subsets of the data, then combine them later. This is called [*partial aggregation*][postgresql-user-defined-aggregates-partial-aggregates] and enables different worker processes to handle data in parallel. Let's make our `example_sum` aggregate above have a `combinefunc`:

*(Readers may note we could use `example_sum_state` in this particular case, but not in general, so we're gonna make a new function for demonstration.)*

```sql
CREATE FUNCTION example_sum_combine(
    first   integer,
    second  integer
) RETURNS integer
LANGUAGE SQL
STRICT
AS $$
    SELECT $1 + $2;
$$;

DROP AGGREGATE example_sum(integer);

CREATE AGGREGATE example_sum(integer)
(
    SFUNC    = example_sum_state,
    STYPE    = integer,
    INITCOND = '0',
    combinefunc = example_sum_combine
);

SELECT example_sum(value) FROM generate_series(0, 4000) as value;
--  example_sum 
-- -------------
--      8002000
-- (1 row)
```

Here's one using `FINALFUNC`, which offers a way to compute some final value from the state:

```sql
CREATE FUNCTION example_uniq_state(
    state text[],
    next  text
) RETURNS text[]
LANGUAGE SQL
STRICT
AS $$
    SELECT array_append($1, $2);
$$;

CREATE FUNCTION example_uniq_final(
    state text[]
) RETURNS integer
LANGUAGE SQL
STRICT
AS $$
    SELECT count(DISTINCT value) FROM UNNEST(state) as value
$$;

CREATE AGGREGATE example_uniq(text)
(
    SFUNC     = example_uniq_state, -- State function
    STYPE     = text[],             -- State type
    INITCOND  = '{}',               -- Must be a string or null
    FINALFUNC = example_uniq_final  -- Final function
);

SELECT example_uniq(value) FROM UNNEST(ARRAY ['a', 'a', 'b']) as value;
--  example_uniq 
-- --------------
--             2
-- (1 row)
```

This is particularly handy as your `STYPE` doesn't need to be the type you return!

Aggregates can take multiple arguments, too:

```sql
CREATE FUNCTION example_concat_state(
    state text[],
    first text,
    second text,
    third text
) RETURNS text[]
LANGUAGE SQL
STRICT
AS $$
    SELECT array_append($1, concat($2, $3, $4));
$$;

CREATE AGGREGATE example_concat(text, text, text)
(
    SFUNC     = example_concat_state,
    STYPE     = text[],
    INITCOND  = '{}'
);

SELECT example_concat(first, second, third) FROM
    UNNEST(ARRAY ['a', 'b', 'c']) as first,
    UNNEST(ARRAY ['1', '2', '3']) as second,
    UNNEST(ARRAY ['!', '@', '#']) as third;
--                                                 example_concat                                                 
-- ---------------------------------------------------------------------------------------------------------------
--  {a1!,a2!,a3!,b1!,b2!,b3!,c1!,c2!,c3!,a1@,a2@,a3@,b1@,b2@,b3@,c1@,c2@,c3@,a1#,a2#,a3#,b1#,b2#,b3#,c1#,c2#,c3#}
-- (1 row)
```

See how we see `a1`, `b1`, and `c1`? Multiple arguments might not work as you expect! As you can see, each argument is passed with each other argument.

```sql
SELECT UNNEST(ARRAY ['a', 'b', 'c']) as first,
       UNNEST(ARRAY ['1', '2', '3']) as second,
       UNNEST(ARRAY ['!', '@', '#']) as third;
--  first | second | third 
-- -------+--------+-------
--  a     | 1      | !
--  b     | 2      | @
--  c     | 3      | #
-- (3 rows)
```

Aggregates have several more optional fields, such as a `PARALLEL`. Their signatures are documented in the [`CREATE AGGREGATE`][postgresql-create-aggregate] documentation and this article isn't meant to be comprehensive.

> **Reminder:** You can also create functions with [`pl/pgsql`][postgresql-plpgsql], [`c`][postgresql-xfunc-c], [pl/Python][postgresql-plpython], or even in the experimental [`pl/Rust`][plrust].

Extensions can, of course, create aggregates too. Next, let's explore how to do that with Rust using [`pgrx`][pgrx] 0.3.0's Aggregate support.

# Familiarizing with `pgrx`

[`pgrx`][pgrx] is a suite of crates that provide everything required to build, test, and package extensions for PostgreSQL versions 11 through 15 using pure Rust.

It includes:

* `cargo-pgrx`: A `cargo` plugin that provides commands like `cargo pgrx package` and `cargo pgrx test`,
* `pgrx`: A crate providing macros, high level abstractions (such as SPI), and low level generated bindings for PostgreSQL.
* `pgrx-tests`: A crate providing a test framework for running tests inside PostgreSQL.

**Note:** `pgrx` does not currently offer Windows support, but works great in [WSL2][wsl].

If a Rust toolchain is not already installed, please follow the instructions on [rustup.rs][rustup-rs].

You'll also [need to make sure you have some development libraries][pgrx-system-requirements] like `zlib` and `libclang`, as 
`cargo pgrx init` will, by default, build it's own development PostgreSQL installs. Usually it's possible to
figure out if something is missing from error messages and then discover the required package for the system.

Install `cargo-pgrx` then initialize its development PostgreSQL installations (used for `cargo pgrx test` and `cargo pgrx run`):

```bash
$ cargo install --locked cargo-pgrx
$ cargo pgrx init
# ...
```

We can create a new extension with:

```bash
$ cargo pgrx new exploring_aggregates
$ cd exploring_aggregates
```

Then run it:

```bash
$ cargo pgrx run
# ...
building extension with features ``
"cargo" "build"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s

installing extension
     Copying control file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates.control`
     Copying shared library to `/home/ana/.pgrx/13.5/pgrx-install/lib/postgresql/exploring_aggregates.so`
 Discovering SQL entities
  Discovered 1 SQL entities: 0 schemas (0 unique), 1 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes
running SQL generator
"/home/ana/git/samples/exploring_aggregates/target/debug/sql-generator" "--sql" "/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql"
     Copying extension schema file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql`
    Finished installing exploring_aggregates
    Starting Postgres v13 on port 28813
     Creating database exploring_aggregates
psql (13.5)
Type "help" for help.

exploring_aggregates=# 
```

Observing the start of the `src/lib.rs` file, we can see the `pg_module_magic!()` and a function `hello_exploring_aggregates`:

```rust
use pgrx::*;

pg_module_magic!();

#[pg_extern]
fn hello_exploring_aggregates() -> &'static str {
    "Hello, exploring_aggregates"
}
```

Back on our `psql` prompt, we can load the extension and run the function:

```sql
CREATE EXTENSION exploring_aggregates;
-- CREATE EXTENSION

\dx+ exploring_aggregates
-- Objects in extension "exploring_aggregates"
--           Object description           
-- ---------------------------------------
--  function hello_exploring_aggregates()
-- (1 row)

SELECT hello_exploring_aggregates();
--  hello_exploring_aggregates  
-- -----------------------------
--  Hello, exploring_aggregates
-- (1 row)
```

Next, let's run the tests:

```bash
$ cargo pgrx test
"cargo" "test" "--features" " pg_test"
    Finished test [unoptimized + debuginfo] target(s) in 0.08s
     Running unittests (target/debug/deps/exploring_aggregates-4783beb51375d29c)

running 1 test
building extension with features ` pg_test`
"cargo" "build" "--features" " pg_test"
    Finished dev [unoptimized + debuginfo] target(s) in 0.41s

installing extension
     Copying control file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates.control`
     Copying shared library to `/home/ana/.pgrx/13.5/pgrx-install/lib/postgresql/exploring_aggregates.so`
 Discovering SQL entities
  Discovered 3 SQL entities: 1 schemas (1 unique), 2 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 0 aggregates
running SQL generator
"/home/ana/git/samples/exploring_aggregates/target/debug/sql-generator" "--sql" "/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql"
     Copying extension schema file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql`
    Finished installing exploring_aggregates
test tests::pg_test_hello_exploring_aggregates ... ok

test result: ok. 1 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 2.44s

Stopping Postgres

     Running unittests (target/debug/deps/sql_generator-1bb38131b30894e5)

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s

   Doc-tests exploring_aggregates

running 0 tests

test result: ok. 0 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.00s
```

We can also inspect the SQL the extension generates:

```bash
$ cargo pgrx schema
    Building SQL generator with features ``
"cargo" "build" "--bin" "sql-generator"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s
 Discovering SQL entities
  Discovered 1 SQL entities: 0 schemas (0 unique), 1 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 0 aggregates
running SQL generator
"/home/ana/git/samples/exploring_aggregates/target/debug/sql-generator" "--sql" "sql/exploring_aggregates-0.0.0.sql"
```

This creates `sql/exploring_aggregates-0.0.0.sql`:

```sql
/* 
This file is auto generated by pgrx.

The ordering of items is not stable, it is driven by a dependency graph.
*/

-- src/lib.rs:5
-- exploring_aggregates::hello_exploring_aggregates
CREATE OR REPLACE FUNCTION "hello_exploring_aggregates"() RETURNS text /* &str */
STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'hello_exploring_aggregates_wrapper';
```

Finally we can create a package for the `pg_config` version installed on the system, this is done in release mode, so it takes a few minutes:

```bash
$ cargo pgrx package
building extension with features ``
"cargo" "build" "--release"
    Finished release [optimized] target(s) in 0.07s

installing extension
 Discovering SQL entities
  Discovered 1 SQL entities: 0 schemas (0 unique), 1 functions, 0 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 0 aggregates
running SQL generator
"/home/ana/git/samples/exploring_aggregates/target/release/sql-generator" "--sql" "/home/ana/git/samples/exploring_aggregates/target/release/exploring_aggregates-pg13/usr/share/postgresql/13/extension/exploring_aggregates--0.0.0.sql"
     Copying extension schema file to `target/release/exploring_aggregates-pg13/usr/share/postgresql/13/extension/exploring_aggregates--0.0.0.sql`
    Finished installing exploring_aggregates
```

Let's make some aggregates with `pgrx` now!

# Aggregates with [`pgrx`][pgrx]

While designing the aggregate support for [`pgrx`][pgrx] 0.3.0 we wanted to try to make things feel idiomatic and natural from the Rust side,
but it should be flexible enough for any use.

Aggregates in `pgrx` are defined by creating a type (this doesn't necessarily need to be the state type), then using the [`#[pg_aggregate]`][pgrx-pg_aggregate]
procedural macro on an [`pgrx::Aggregate`][pgrx-aggregate-aggregate] implementation for that type.

The [`pgrx::Aggregate`][pgrx-aggregate-aggregate] trait has quite a few items (`fn`s, `const`s, `type`s) that you can implement, but the procedural macro can fill in
stubs for all non-essential items. The state type (the implementation target by default) must have a [`#[derive(PostgresType)]`][pgrx-postgrestype] declaration,
or be a type PostgreSQL already knows about.

Here's the simplest aggregate you can make with `pgrx`:

```rust
use pgrx::*;
use serde::{Serialize, Deserialize};

pg_module_magic!();

#[derive(Copy, Clone, Default, Debug, PostgresType, Serialize, Deserialize)]
pub struct DemoSum {
    count: i32,
}

#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"{ "count": 0 }"#);
    type Args = i32;
    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        current.count += arg;
        current
    }
}
```

We can review the generated SQL (generated via `cargo pgrx schema`):

```sql
/* 
This file is auto generated by pgrx.

The ordering of items is not stable, it is driven by a dependency graph.
*/

-- src/lib.rs:6
-- exploring_aggregates::DemoSum
CREATE TYPE DemoSum;

-- src/lib.rs:6
-- exploring_aggregates::demosum_in
CREATE OR REPLACE FUNCTION "demosum_in"(
	"input" cstring /* &std::ffi::CStr */
) RETURNS DemoSum /* exploring_aggregates::DemoSum */
IMMUTABLE PARALLEL SAFE STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'demosum_in_wrapper';

-- src/lib.rs:6
-- exploring_aggregates::demosum_out
CREATE OR REPLACE FUNCTION "demosum_out"(
	"input" DemoSum /* exploring_aggregates::DemoSum */
) RETURNS cstring /* &std::ffi::CStr */
IMMUTABLE PARALLEL SAFE STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'demosum_out_wrapper';

-- src/lib.rs:6
-- exploring_aggregates::DemoSum
CREATE TYPE DemoSum (
	INTERNALLENGTH = variable,
	INPUT = demosum_in, /* exploring_aggregates::demosum_in */
	OUTPUT = demosum_out, /* exploring_aggregates::demosum_out */
	STORAGE = extended
);

-- src/lib.rs:11
-- exploring_aggregates::demo_sum_state
CREATE OR REPLACE FUNCTION "demo_sum_state"(
	"this" DemoSum, /* exploring_aggregates::DemoSum */
	"arg_one" integer /* i32 */
) RETURNS DemoSum /* exploring_aggregates::DemoSum */
STRICT
LANGUAGE c /* Rust */
AS 'MODULE_PATHNAME', 'demo_sum_state_wrapper';

-- src/lib.rs:11
-- exploring_aggregates::DemoSum
CREATE AGGREGATE DemoSum (
	integer /* i32 */
)
(
	SFUNC = "demo_sum_state", /* exploring_aggregates::DemoSum::state */
	STYPE = DemoSum, /* exploring_aggregates::DemoSum */
	INITCOND = '{ "count": 0 }' /* exploring_aggregates::DemoSum::INITIAL_CONDITION */
);
```

We can test it out with `cargo pgrx run`:

```bash
$ cargo pgrx run
    Stopping Postgres v13
building extension with features ``
"cargo" "build"
    Finished dev [unoptimized + debuginfo] target(s) in 0.06s

installing extension
     Copying control file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates.control`
     Copying shared library to `/home/ana/.pgrx/13.5/pgrx-install/lib/postgresql/exploring_aggregates.so`
 Discovering SQL entities
  Discovered 5 SQL entities: 0 schemas (0 unique), 3 functions, 1 types, 0 enums, 0 sqls, 0 ords, 0 hashes, 1 aggregates
running SQL generator
"/home/ana/git/samples/exploring_aggregates/target/debug/sql-generator" "--sql" "/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql"
     Copying extension schema file to `/home/ana/.pgrx/13.5/pgrx-install/share/postgresql/extension/exploring_aggregates--0.0.0.sql`
    Finished installing exploring_aggregates
    Starting Postgres v13 on port 28813
    Re-using existing database exploring_aggregates
psql (13.5)
Type "help" for help.

exploring_aggregates=# 
```

Now we're connected via `psql`:

```sql
CREATE EXTENSION exploring_aggregates;
-- CREATE EXTENSION

SELECT DemoSum(value) FROM generate_series(0, 4000) as value;
--       demosum      
-- -------------------
--  {"count":8002000}
-- (1 row)
```

Pretty cool!

...But we don't want that silly `{"count": ... }` stuff, just the number! We can resolve this by changing the [`State`][pgrx-aggregate-aggregate-state] type, or by adding a [`finalize`][pgrx-aggregate-aggregate-finalize] (which maps to `ffunc`) as we saw in the previous section.

Let's change the [`State`][pgrx-aggregate-aggregate-state] this time:

```rust
#[derive(Copy, Clone, Default, Debug)]
pub struct DemoSum;

#[pg_aggregate]
impl Aggregate for DemoSum {
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"0"#);
    type Args = i32;
    type State = i32;

    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        current += arg;
        current
    }
}
```

Now when we run it:

```sql
SELECT DemoSum(value) FROM generate_series(0, 4000) as value;
--  demosum 
-- ---------
--  8002000
-- (1 row)
```

This is a fine reimplementation of `SUM` so far, but as we saw previously we need a [`combine`][pgrx-aggregate-aggregate-combine] (mapping to `combinefunc`) to support partial aggregation:

```rust
#[pg_aggregate]
impl Aggregate for DemoSum {
    // ...
    fn combine(
        mut first: Self::State,
        second: Self::State,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        first += second;
        first
    }
}
```

We can also change the name of the generated aggregate, or set the [`PARALLEL`][pgrx-aggregate-aggregate-parallel] settings, for example:

```rust
#[pg_aggregate]
impl Aggregate for DemoSum {
    // ...
    const NAME: &'static str = "demo_sum";
    const PARALLEL: Option<ParallelOption> = Some(pgrx::aggregate::ParallelOption::Unsafe);
    // ...
}
```

This generates:

```sql
-- src/lib.rs:9
-- exploring_aggregates::DemoSum
CREATE AGGREGATE demo_sum (
	integer /* i32 */
)
(
	SFUNC = "demo_sum_state", /* exploring_aggregates::DemoSum::state */
	STYPE = integer, /* i32 */
	COMBINEFUNC = "demo_sum_combine", /* exploring_aggregates::DemoSum::combine */
	INITCOND = '0', /* exploring_aggregates::DemoSum::INITIAL_CONDITION */
	PARALLEL = UNSAFE /* exploring_aggregates::DemoSum::PARALLEL */
);
```

## Rust state types

It's possible to use a non-SQL (say, [`HashSet<String>`][std::collections::HashSet]) type as a state by using [`Internal`][pgrx::datum::Internal].

> When using this strategy, **a `finalize` function must be provided.**

Here's a unique string counter aggregate that uses a `HashSet`:

```rust
use pgrx::*;
use std::collections::HashSet;

pg_module_magic!();

#[derive(Copy, Clone, Default, Debug)]
pub struct DemoUnique;

#[pg_aggregate]
impl Aggregate for DemoUnique {
    type Args = &'static str;
    type State = Internal;
    type Finalize = i32;

    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let inner = unsafe { current.get_or_insert_default::<HashSet<String>>() };

        inner.insert(arg.to_string());
        current
    }

    fn combine(
        mut first: Self::State,
        mut second: Self::State,
        _fcinfo: pg_sys::FunctionCallInfo
    ) -> Self::State {
        let first_inner = unsafe { first.get_or_insert_default::<HashSet<String>>() };
        let second_inner = unsafe { second.get_or_insert_default::<HashSet<String>>() };

        let unioned: HashSet<_> = first_inner.union(second_inner).collect();
        Internal::new(unioned)
    }

    fn finalize(
        mut current: Self::State,
        _direct_arg: Self::OrderedSetArgs,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::Finalize {
        let inner = unsafe { current.get_or_insert_default::<HashSet<String>>() };

        inner.len() as i32
    }
}
```

We can test it:

```sql
SELECT DemoUnique(value) FROM UNNEST(ARRAY ['a', 'a', 'b']) as value;
--  demounique 
-- ------------
--           2
-- (1 row)
```

Using `Internal` here means that the values it holds get dropped at the end of [` PgMemoryContexts::CurrentMemoryContext`][postgresql-current-memory-contexts], the aggregate context in this case.


## Ordered-Set Aggregates

PostgreSQL also supports what are called [*Ordered-Set Aggregates*][postgresql-ordered-set-aggregate]. Ordered-Set Aggregates can take a **direct argument**, and specify a sort ordering for the inputs.

> PostgreSQL does *not* order inputs behind the scenes!

Let's create a simple `percentile_disc` reimplementation to get an idea of how to make one with `pgrx`. You'll notice we add [`ORDERED_SET = true`][pgrx::aggregate::Aggregate::ORDERED_SET] and set an (optional) [`OrderedSetArgs`][pgrx::aggregate::Aggregate::OrderedSetArgs], which determines the direct arguments.

```rust
#[derive(Copy, Clone, Default, Debug)]
pub struct DemoPercentileDisc;

#[pg_aggregate]
impl Aggregate for DemoPercentileDisc {
    type Args = name!(input, i32);
    type State = Internal;
    type Finalize = i32;
    const ORDERED_SET: bool = true;
    type OrderedSetArgs = name!(percentile, f64);

    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        let inner = unsafe { current.get_or_insert_default::<Vec<i32>>() };

        inner.push(arg);
        current
    }

    fn finalize(
        mut current: Self::State,
        direct_arg: Self::OrderedSetArgs,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::Finalize {
        let inner = unsafe { current.get_or_insert_default::<Vec<i32>>() };
        // This isn't done for us!
        inner.sort();

        let target_index = (inner.len() as f64 * direct_arg).round() as usize;
        inner[target_index.saturating_sub(1)]
    }
}
```

This creates SQL like:

```sql
-- src/lib.rs:9
-- exploring_aggregates::DemoPercentileDisc
CREATE AGGREGATE DemoPercentileDisc (
	"percentile" double precision /* f64 */
	ORDER BY
	"input" integer /* i32 */
)
(
	SFUNC = "demo_percentile_disc_state", /* exploring_aggregates::DemoPercentileDisc::state */
	STYPE = internal, /* pgrx::datum::internal::Internal */
	FINALFUNC = "demo_percentile_disc_finalize" /* exploring_aggregates::DemoPercentileDisc::final */
);
```

We can test it like so:

```sql
SELECT DemoPercentileDisc(0.5) WITHIN GROUP (ORDER BY income) FROM UNNEST(ARRAY [6000, 70000, 500]) as income;
--  demopercentiledisc 
-- --------------------
--                6000
-- (1 row)

SELECT DemoPercentileDisc(0.05) WITHIN GROUP (ORDER BY income) FROM UNNEST(ARRAY [5, 100000000, 6000, 70000, 500]) as income;
--  demopercentiledisc 
-- --------------------
--                   5
-- (1 row)
```

## Moving-Aggregate mode

Aggregates can also support [*moving-aggregate mode*][postgresql-moving-aggregate], which can **remove** inputs from the aggregate as well.

This allows for some optimization if you are using aggregates as window functions. The documentation explains that this is because PostgreSQL doesn't need to recalculate the aggregate each time the frame starting point moves.

Moving-aggregate mode has it's own [`moving_state`][pgrx::aggregate::Aggregate::moving_state] function as well as an [`moving_state_inverse`][pgrx::aggregate::Aggregate::moving_state_inverse] function for removing inputs. Because moving-aggregate mode may require some additional tracking on the part of the aggregate, there is also a [`MovingState`][pgrx::aggregate::Aggregate::MovingState] associated type as well as a [`moving_state_finalize`][pgrx::aggregate::Aggregate::moving_state_finalize] function for any specialized final computation.

Let's take our sum example above and add moving-aggregate mode support to it:

```rust
#[derive(Copy, Clone, Default, Debug)]
pub struct DemoSum;

#[pg_aggregate]
impl Aggregate for DemoSum {
    const NAME: &'static str = "demo_sum";
    const PARALLEL: Option<ParallelOption> = Some(pgrx::aggregate::ParallelOption::Unsafe);
    const INITIAL_CONDITION: Option<&'static str> = Some(r#"0"#);
    const MOVING_INITIAL_CONDITION: Option<&'static str> = Some(r#"0"#);

    type Args = i32;
    type State = i32;
    type MovingState = i32;

    fn state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        pgrx::log!("state({current}, {arg})");
        current += arg;
        current
    }

    fn moving_state(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::MovingState {
        pgrx::log!("moving_state({current}, {arg})");
        current += arg;
        current
    }

    fn moving_state_inverse(
        mut current: Self::State,
        arg: Self::Args,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::MovingState {
        pgrx::log!("moving_state_inverse({current}, {arg})");
        current -= arg;
        current
    }

    fn combine(
        mut first: Self::State,
        second: Self::State,
        _fcinfo: pg_sys::FunctionCallInfo,
    ) -> Self::State {
        pgrx::log!("combine({first}, {second})");
        first += second;
        first
    }
}
```

This generates:

```sql
-- src/lib.rs:8
-- exploring_aggregates::DemoSum
CREATE AGGREGATE demo_sum (
	integer /* i32 */
)
(
	SFUNC = "demo_sum_state", /* exploring_aggregates::DemoSum::state */
	STYPE = integer, /* i32 */
	COMBINEFUNC = "demo_sum_combine", /* exploring_aggregates::DemoSum::combine */
	INITCOND = '0', /* exploring_aggregates::DemoSum::INITIAL_CONDITION */
	MSFUNC = "demo_sum_moving_state", /* exploring_aggregates::DemoSum::moving_state */
	MINVFUNC = "demo_sum_moving_state_inverse", /* exploring_aggregates::DemoSum::moving_state_inverse */
	MINITCOND = '0', /* exploring_aggregates::DemoSum::MOVING_INITIAL_CONDITION */
	PARALLEL = UNSAFE, /* exploring_aggregates::DemoSum::PARALLEL */
	MSTYPE = integer /* exploring_aggregates::DemoSum::MovingState = i32 */
);
```

Using it (we'll also turn on logging to see what happens with `SET client_min_messages TO debug;`):

```sql
SET client_min_messages TO debug;
-- SET

SELECT demo_sum(value) OVER (
    ROWS CURRENT ROW
) FROM UNNEST(ARRAY [1, 20, 300, 4000]) as value;
-- LOG:  moving_state(0, 1)
-- LOG:  moving_state(0, 20)
-- LOG:  moving_state(0, 300)
-- LOG:  moving_state(0, 4000)
--  demo_sum 
-- ----------
--         1
--        20
--       300
--      4000
-- (4 rows)
```

Inside the `OVER ()` we can use [syntax for window function calls][postgresql-window-function-calls].

```sql
SELECT demo_sum(value) OVER (
    ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
) FROM UNNEST(ARRAY [1, 20, 300, 4000]) as value;
-- LOG:  moving_state(0, 1)
-- LOG:  moving_state(1, 20)
-- LOG:  moving_state(21, 300)
-- LOG:  moving_state(321, 4000)
--  demo_sum 
-- ----------
--      4321
--      4321
--      4321
--      4321
-- (4 rows)

SELECT demo_sum(value) OVER (
    ROWS BETWEEN 1 PRECEDING AND CURRENT ROW
) FROM UNNEST(ARRAY [1, 20, 300, 4000]) as value;
-- LOG:  moving_state(0, 1)
-- LOG:  moving_state(1, 20)
-- LOG:  moving_state_inverse(21, 1)
-- LOG:  moving_state(20, 300)
-- LOG:  moving_state_inverse(320, 20)
-- LOG:  moving_state(300, 4000)
--  demo_sum 
-- ----------
--         1
--        21
--       320
--      4300
-- (4 rows)

SELECT demo_sum(value) OVER (
    ORDER BY sorter
    ROWS BETWEEN CURRENT ROW AND 1 FOLLOWING
) FROM (
    VALUES (1, 10000),
           (2, 1)
    ) AS v (sorter, value);
-- LOG:  moving_state(0, 10000)
-- LOG:  moving_state(10000, 1)
-- LOG:  moving_state_inverse(10001, 10000)
--  demo_sum 
-- ----------
--     10001
--         1
-- (2 rows)
```

# Wrapping up

I had a lot of fun implementing the aggregate support for [`pgrx`][pgrx], and hope you have just as much fun using it! If you have questions, open up an [issue][pgrx-issues].

Moving-aggregate mode is pretty new to me, and I'm still learning about it! If you have any good resources I'd love to receive them from you!

If you're looking for more materials about aggregates, the TimescaleDB folks wrote about aggregates and how they impacted their hyperfunctions in [this article][timescaledb-article-aggregation]. Also, My pal [Tim McNamara][timclicks] wrote about how to implement harmonic and geometric means as aggregates in [this article][timclicks-article-aggregates].

[pgrx]: https://github.com/pgcentralfoundation/pgrx/
[pgrx-issues]: https://github.com/pgcentralfoundation/pgrx/issues
[pgrx-aggregate-aggregate]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html
[pgrx-aggregate-aggregate-finalize]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#tymethod.finalize
[pgrx-aggregate-aggregate-state]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#associatedtype.State
[pgrx-aggregate-aggregate-combine]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#tymethod.combine
[pgrx-aggregate-aggregate-parallel]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#associatedconstant.PARALLEL
[pgrx::aggregate::Aggregate::ORDERED_SET]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#associatedconstant.ORDERED_SET
[pgrx::aggregate::Aggregate::OrderedSetArgs]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#associatedtype.OrderedSetArgs
[pgrx::aggregate::Aggregate::moving_state]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#tymethod.moving_state
[pgrx::aggregate::Aggregate::moving_state_inverse]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#tymethod.moving_state_inverse
[pgrx::aggregate::Aggregate::MovingState]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#associatedtype.MovingState
[pgrx::aggregate::Aggregate::moving_state_finalize]: https://docs.rs/pgrx/latest/pgrx/aggregate/trait.Aggregate.html#tymethod.moving_finalize
[pgrx::datum::Internal]: https://docs.rs/pgrx/latest/pgrx/datum/struct.Internal.html
[pgrx-pg_aggregate]: https://docs.rs/pgrx/latest/pgrx/attr.pg_aggregate.html
[pgrx-postgrestype]: https://docs.rs/pgrx/latest/pgrx/derive.PostgresType.html
[pgrx-system-requirements]: https://github.com/pgcentralfoundation/pgrx/#system-requirements
[plrust]: https://github.com/tcdi/plrust
[rustup-rs]: https://rustup.rs/
[wsl]: https://docs.microsoft.com/en-us/windows/wsl/install
[std::iter::Iterator]: https://doc.rust-lang.org/std/iter/trait.Iterator.html
[std::iter::Iterator::all]: https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.all
[std::iter::Iterator::any]: https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.any
[std::iter::Iterator::collect]: https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.collect
[std::iter::Iterator::fold]: https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.collect
[std::collections::hashset]: https://doc.rust-lang.org/std/collections/struct.HashSet.html
[postgresql-pl-pgsql]: https://www.postgresql.org/docs/current/plpgsql.html
[postgresql-user-defined-aggregates]: https://www.postgresql.org/docs/current/xaggr.html
[postgresql-user-defined-aggregates-partial-aggregates]: https://www.postgresql.org/docs/current/xaggr.html#XAGGR-PARTIAL-AGGREGATES
[postgresql-aggregate-functions]: https://www.postgresql.org/docs/current/functions-aggregate.html
[postgresql-window-function-calls]: https://www.postgresql.org/docs/current/sql-expressions.html#SYNTAX-WINDOW-FUNCTIONS
[postgresql-create-aggregate]: https://www.postgresql.org/docs/current/sql-createaggregate.html
[postgresql-ordered-set-aggregate]: https://www.postgresql.org/docs/current/xaggr.html#XAGGR-ORDERED-SET-AGGREGATES
[postgresql-moving-aggregate]: https://www.postgresql.org/docs/current/xaggr.html#XAGGR-MOVING-AGGREGATES
[postgresql-create-function]: https://www.postgresql.org/docs/current/sql-createfunction.html
[postgresql-current-memory-contexts]: https://github.com/postgres/postgres/blob/7c1aead6cbe7dcc6c216715fed7a1fb60684c5dc/src/backend/utils/mmgr/README#L72
[postgresql-xfunc-c]: https://www.postgresql.org/docs/current/xfunc-c.html
[postgresql-plpython]: https://www.postgresql.org/docs/current/plpython.html
[postgresql-plpgsql]: https://www.postgresql.org/docs/current/plpgsql.html
[postgis]: https://postgis.net/
[postgis-aggregates]: https://postgis.net/docs/PostGIS_Special_Functions_Index.html#PostGIS_Aggregate_Functions
[postgis-aggregates-3d-extent]: https://postgis.net/docs/ST_3DExtent.html
[timescaledb-article-aggregation]: https://blog.timescale.com/blog/how-postgresql-aggregation-works-and-how-it-inspired-our-hyperfunctions-design-2/
[timclicks]: https://tim.mcnamara.nz/
[timclicks-article-aggregates]: https://tim.mcnamara.nz/post/177172657187/user-defined-aggregates-in-postgresql
[pg-strom]: https://heterodb.github.io/pg-strom/
[pg-strom-aggregates]: https://heterodb.github.io/pg-strom/ref_sqlfuncs/#hyperloglog-functions
