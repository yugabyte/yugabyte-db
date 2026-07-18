use ::pgrx::prelude::*;

fn main() {}

#[pg_extern]
fn gets_name_field(dog: Option<pgrx::composite_type!("Dog")>) -> Option<&str> {
    // Gets resolved to:
    let dog: Option<PgHeapTuple<AllocatedByRust>> = dog;

    dog?.get_by_name("name").ok()?
}

#[pg_extern]
fn gets_name_field_default(
    dog: default!(pgrx::composite_type!("Dog"), "ROW('Nami', 0)::Dog"),
) -> &str {
    // Gets resolved to:
    let dog: PgHeapTuple<AllocatedByRust> = dog;

    dog.get_by_name("name").unwrap().unwrap()
}

#[pg_extern]
fn gets_name_field_strict(dog: pgrx::composite_type!("Dog")) -> &str {
    // Gets resolved to:
    let dog: PgHeapTuple<AllocatedByRust> = dog;

    dog.get_by_name("name").unwrap().unwrap()
}
