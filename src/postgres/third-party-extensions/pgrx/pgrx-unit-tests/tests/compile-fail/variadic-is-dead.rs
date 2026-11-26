use pgrx::prelude::*;

fn main() {}
#[pg_extern]
fn whatever() -> variadic!(i32) {
    todo!()
}
