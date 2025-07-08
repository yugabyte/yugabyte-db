use pgrx::prelude::*;

fn main() {}

const DOG_COMPOSITE_TYPE: &str = "Dog";

#[pg_extern]
fn gets_name_field_variadic(
    dogs: VariadicArray<pgrx::composite_type!(DOG_COMPOSITE_TYPE)>,
) -> Vec<String> {
    // Gets resolved to:
    let dogs: pgrx::VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut names = Vec::with_capacity(dogs.len());
    for dog in dogs {
        let dog = dog.unwrap();
        let name = dog.get_by_name("name").unwrap().unwrap();
        names.push(name);
    }
    names
}

/// TODO: add test which actually calls this maybe???
#[pg_extern]
fn gets_name_field_default_variadic(
    dogs: default!(VariadicArray<pgrx::composite_type!("Dog")>, "ARRAY[ROW('Nami', 0)]::Dog[]"),
) -> Vec<String> {
    // Gets resolved to:
    let dogs: pgrx::VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut names = Vec::with_capacity(dogs.len());
    for dog in dogs {
        let dog = dog.unwrap();
        let name = dog.get_by_name("name").unwrap().unwrap();
        names.push(name);
    }
    names
}

#[pg_extern]
fn gets_name_field_strict_variadic(
    dogs: pgrx::VariadicArray<pgrx::composite_type!("Dog")>,
) -> Vec<String> {
    // Gets resolved to:
    let dogs: pgrx::VariadicArray<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut names = Vec::with_capacity(dogs.len());
    for dog in dogs {
        let dog = dog.unwrap();
        let name = dog.get_by_name("name").unwrap().unwrap();
        names.push(name);
    }
    names
}

#[pg_extern]
fn sum_scritches_for_names_array(
    dogs: Option<pgrx::Array<pgrx::composite_type!("Dog")>>,
) -> i32 {
    // Gets resolved to:
    let dogs: Option<pgrx::Array<PgHeapTuple<AllocatedByRust>>> = dogs;

    let dogs = dogs.unwrap();
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
fn sum_scritches_for_names_array_default(
    dogs: pgrx::default!(
        pgrx::Array<pgrx::composite_type!("Dog")>,
        "ARRAY[ROW('Nami', 0)]::Dog[]"
    ),
) -> i32 {
    // Gets resolved to:
    let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

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
fn sum_scritches_for_names_array_strict(
    dogs: pgrx::Array<pgrx::composite_type!("Dog")>,
) -> i32 {
    // Gets resolved to:
    let dogs: pgrx::Array<PgHeapTuple<AllocatedByRust>> = dogs;

    let mut sum_scritches = 0;
    for dog in dogs {
        let dog = dog.unwrap();
        let scritches: i32 =
            dog.get_by_name("scritches").ok().unwrap_or_default().unwrap_or_default();
        sum_scritches += scritches;
    }
    sum_scritches
}
