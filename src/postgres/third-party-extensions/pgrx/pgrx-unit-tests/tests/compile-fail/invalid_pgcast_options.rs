use pgrx::prelude::*;

#[pg_cast(invalid_opt)]
pub fn cast_function(foo: i32) -> i32 {
    foo
}

fn main() {}
