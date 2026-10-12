use pgrx::prelude::*;

#[pg_cast(implicit, assignment)]
pub fn cast_function(foo: i32) -> i32 {
    foo
}

fn main() {}
