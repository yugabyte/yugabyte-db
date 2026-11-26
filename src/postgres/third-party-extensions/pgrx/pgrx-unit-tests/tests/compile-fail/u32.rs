use pgrx::prelude::*;

fn main() {}

#[pg_extern]
pub fn whatever(a: u32) -> u32 {
    a
}
