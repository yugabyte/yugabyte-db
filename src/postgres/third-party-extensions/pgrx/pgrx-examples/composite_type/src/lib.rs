//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/** A guided example of composite types in `pgrx`

Composite types in `pgrx` are essentially macros that alias to a [`PgHeapTuple`][pgrx::PgHeapTuple]
with one important difference:

During SQL generation, the composite type name provided to the [`pgrx::composite_type`][pgrx::composite_type]
macro is used as the type name, whereas [`PgHeapTuple`][pgrx::PgHeapTuple] does not have a valid
Rust to SQL mapping.

A number of considerations are detailed in the [`pgrx::composite_type`][pgrx::composite_type] and
[`PgHeapTuple`][pgrx::PgHeapTuple] documentation which may assist in productionalizing extensions
using composite types.
*/
use pgrx::{opname, pg_operator, prelude::*};

// All `pgrx` extensions will do this:
::pgrx::pg_module_magic!();

/* Composite types must be defined either before they are used.

This means composite types must:
* Be defined prior to loading the extension
* Be defined by an `extension_sql!()` block which the function has been "ordered before" in
    the generated SQL

If your extension depends on already existing composite types, then you don't need to do
anything to define them. Just use them as if they exist.

If your extension defines the composite types itself, it's recommended to do that in an
`extension_sql!()` which is set to be a `bootstrap`, and is ordered first in the generated SQL:
*/
extension_sql!(
    "\
CREATE TYPE Dog AS (
    name TEXT,
    scritches INT
);

CREATE TYPE Cat AS (
    name TEXT,
    boops INT
);",
    name = "create_composites",
    bootstrap
);

/*
If it's required for exotic reasons that the composite type not be part of the bootstrap code,
it can also be defined in a non-bootstrap SQL block (**Note:** This could have just been
included in the above bootstrap code):
*/
extension_sql!(
    "\
CREATE TYPE CatAndDogFriendship AS (
    cat Cat,
    dog Dog
);",
    name = "create_cat_and_dog_friendship",
);

// To assist with code reuse, consider setting your composite type names in constants:
const DOG_COMPOSITE_TYPE: &str = "Dog";
const CAT_COMPOSITE_TYPE: &str = "Cat";
const CAT_AND_DOG_FRIENDSHIP_COMPOSITE_TYPE: &str = "CatAndDogFriendship";

/*
While it may be tempting to do something like:

```rust
const CAT_COMPOSITE_TYPE: PgHeapTuple<'static, AllocatedByRust> = pgrx::composite_type!("Cat");
```
This is not correct! The [`#[pgrx::pg_extern]`][pgrx::pg_extern] macro would not be able to pick
up the required metadata for SQL generation. Instead, set the constant as a `&'static str` which
the [`pgrx::composite_type!()`][pgrx::composite_type] macro can consume.
*/

/*
Create a dog inside Rust then return it, roughly the equivalent of this SQL:

```sql
CREATE FUNCTION create_dog(name TEXT, scritches INT) RETURNS Dog
    LANGUAGE SQL
    STRICT RETURN ROW(name, scritches)::Dog;
```
*/
#[pg_extern]
fn create_dog(name: String, scritches: i32) -> pgrx::composite_type!('static, DOG_COMPOSITE_TYPE) {
    let mut dog = PgHeapTuple::new_composite_type(DOG_COMPOSITE_TYPE).unwrap();
    dog.set_by_name("name", name).unwrap();
    dog.set_by_name("scritches", scritches).unwrap();
    dog
}

/*
Scritch the passed dog, then return it, roughly the equivalent of this SQL:

```sql
CREATE FUNCTION scritch_dog(dog Dog) RETURNS Dog
    LANGUAGE SQL
    STRICT RETURN ROW(dog.name, dog.scritches + 1)::Dog;
```
*/
#[pg_extern]
fn scritch_dog(
    mut dog: pgrx::composite_type!(DOG_COMPOSITE_TYPE),
) -> pgrx::composite_type!(DOG_COMPOSITE_TYPE) {
    if let Ok(scritches) = dog.get_by_name::<i32>("scritches") {
        dog.set_by_name("scritches", scritches).unwrap();
    }
    dog
}

/*
Make a cat and a dog friends, returning that relationship, roughly the same as:

```sql
CREATE FUNCTION make_friendship(dog Dog, cat Cat)
    RETURNS CatAndDogFriendship
    LANGUAGE SQL
    STRICT
    RETURN ROW(cat, dog)::CatAndDogFriendship;
```
This function primarily exists to demonstrate how to make some `extension_sql!()` "appear before"
the function.
*/
#[pg_extern(requires = ["create_cat_and_dog_friendship"])]
fn make_friendship(
    dog: pgrx::composite_type!(DOG_COMPOSITE_TYPE),
    cat: pgrx::composite_type!(CAT_COMPOSITE_TYPE),
) -> pgrx::composite_type!('static, CAT_AND_DOG_FRIENDSHIP_COMPOSITE_TYPE) {
    let mut friendship =
        PgHeapTuple::new_composite_type(CAT_AND_DOG_FRIENDSHIP_COMPOSITE_TYPE).unwrap();
    friendship.set_by_name("dog", dog).unwrap();
    friendship.set_by_name("cat", cat).unwrap();
    friendship
}

/*
FIXME: make this example no longer DOA
Create sum the scritches received by dogs, roughly the equivalent of:

```sql
CREATE FUNCTION sum_scritches_state(state integer, new Dog)
    RETURNS integer
    LANGUAGE SQL
    STRICT
    RETURN state + new.scritches;

CREATE AGGREGATE sum_scritches ("value" Dog) (
    SFUNC = "sum_scritches_state",
    STYPE = integer,
    INITCOND = '0'
)
```
*/
// struct SumScritches;

/*
Create an operator allowing dogs to accept scritches directly.

```sql
CREATE FUNCTION add_scritches_to_dog(
    dog Dog,
    number integer
) RETURNS Dog
LANGUAGE SQL
STRICT
RETURN ROW(dog.name, dog.scritches + number)::Dog;

CREATE OPERATOR + (
    PROCEDURE="add_scritches_to_dog",
    LEFTARG=Dog,
    RIGHTARG=integer
);
```
*/
#[pg_operator]
#[opname(+)]
fn add_scritches_to_dog(
    mut left: pgrx::composite_type!("Dog"),
    right: i32,
) -> pgrx::composite_type!("Dog") {
    let left_scritches: i32 = left.get_by_name("scritches").unwrap().unwrap_or_default();
    left.set_by_name("scritches", left_scritches + right).unwrap();
    left
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;
    use pgrx::AllocatedByRust;

    #[pg_test]
    fn test_create_dog() -> Result<(), pgrx::spi::Error> {
        let retval = Spi::get_one::<PgHeapTuple<AllocatedByRust>>(
            "\
            SELECT create_dog('Nami', 0)
        ",
        )?
        .expect("SQL select failed");
        assert_eq!(retval.get_by_name::<&str>("name")?.unwrap(), "Nami");
        assert_eq!(retval.get_by_name::<i32>("scritches")?.unwrap(), 0);
        Ok(())
    }

    #[pg_test]
    fn test_scritch_dog() -> Result<(), pgrx::spi::Error> {
        let retval = Spi::get_one::<PgHeapTuple<AllocatedByRust>>(
            "\
            SELECT scritch_dog(ROW('Nami', 1)::Dog)
        ",
        )?
        .expect("SQL select failed");
        assert_eq!(retval.get_by_name::<&str>("name")?.unwrap(), "Nami");
        assert_eq!(retval.get_by_name::<i32>("scritches")?.unwrap(), 1);
        Ok(())
    }

    #[pg_test]
    fn test_make_friendship() -> Result<(), pgrx::spi::Error> {
        let friendship = Spi::get_one::<PgHeapTuple<AllocatedByRust>>(
            "\
            SELECT make_friendship(ROW('Nami', 0)::Dog, ROW('Sally', 0)::Cat)
        ",
        )?
        .expect("SQL select failed");
        let dog: PgHeapTuple<AllocatedByRust> = friendship.get_by_name("dog")?.unwrap();
        assert_eq!(dog.get_by_name::<&str>("name")?.unwrap(), "Nami");

        let cat: PgHeapTuple<AllocatedByRust> = friendship.get_by_name("cat")?.unwrap();
        assert_eq!(cat.get_by_name::<&str>("name")?.unwrap(), "Sally");
        Ok(())
    }

    #[pg_test]
    fn test_dog_add_operator() {
        let retval = Spi::get_one::<i32>("SELECT (ROW('Nami', 0)::Dog + 1).scritches;");
        assert_eq!(retval, Ok(Some(1)));
    }
}

#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}
