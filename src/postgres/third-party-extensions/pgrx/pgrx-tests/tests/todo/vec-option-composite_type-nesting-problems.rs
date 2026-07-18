//! TODO: This probably requires the FunctionArguments<'curr, 'fn> type
use pgrx::prelude::*;

fn main() {}

const DOG_COMPOSITE_TYPE: &str = "Dog";

#[pg_extern]
fn sum_scritches_for_names(dogs: Option<Vec<pgrx::composite_type!(DOG_COMPOSITE_TYPE)>>) -> i32 {
    // Gets resolved to:
    let dogs: Option<Vec<PgHeapTuple<AllocatedByRust>>> = dogs;

    let dogs = dogs.unwrap();
    let mut sum_scritches = 0;
    for dog in dogs {
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}

#[pg_extern]
fn sum_scritches_for_names_default(
    dogs: pgrx::default!(Vec<pgrx::composite_type!("Dog")>, "ARRAY[ROW('Nami', 0)]::Dog[]"),
) -> i32 {
    // Gets resolved to:
    let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut sum_scritches = 0;
    for dog in dogs {
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}

#[pg_extern]
fn sum_scritches_for_names_strict(dogs: Vec<pgrx::composite_type!("Dog")>) -> i32 {
    // Gets resolved to:
    let dogs: Vec<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut sum_scritches = 0;
    for dog in dogs {
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}

#[pg_extern]
fn sum_scritches_for_names_strict_optional_items(
    dogs: Vec<Option<pgrx::composite_type!("Dog")>>,
) -> i32 {
    // Gets resolved to:
    let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

    let mut sum_scritches = 0;
    for dog in dogs {
        let dog = dog.unwrap();
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}

#[pg_extern]
fn sum_scritches_for_names_default_optional_items(
    dogs: pgrx::default!(Vec<Option<pgrx::composite_type!("Dog")>>, "ARRAY[ROW('Nami', 0)]::Dog[]"),
) -> i32 {
    // Gets resolved to:
    let dogs: Vec<Option<PgHeapTuple<AllocatedByRust>>> = dogs;

    let mut sum_scritches = 0;
    for dog in dogs {
        let dog = dog.unwrap();
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}

#[pg_extern]
fn sum_scritches_for_names_optional_items(
    dogs: Option<Vec<Option<pgrx::composite_type!("Dog")>>>,
) -> i32 {
    // Gets resolved to:
    let dogs: Option<Vec<Option<PgHeapTuple<AllocatedByRust>>>> = dogs;

    let dogs = dogs.unwrap();
    let mut sum_scritches = 0;
    for dog in dogs {
        let dog = dog.unwrap();
        let scritches: i32 = dog.get_by_name("scritches").unwrap().unwrap();
        sum_scritches += scritches;
    }
    sum_scritches
}
