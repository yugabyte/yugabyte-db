use pgrx::array::FlatArray;
use pgrx::palloc::PBox;
use pgrx::prelude::*;

// We can support this behavior, it just requires allocating a fresh instance,
// which is something we want to approach systematically for similar datums.
#[pg_extern]
fn something<'a>(_arr: PBox<'a, FlatArray<'a, i32>>) {
    todo!()
}

fn main() {}
