use pgrx::array::FlatArray;
use pgrx::prelude::*;

// We could support this behavior but it would require allocating a fresh instance,
// which is better represented by using PBox or similar for the input array.
#[pg_extern]
fn something<'a>(_arr: &mut FlatArray<'a, i32>) {
    todo!()
}

fn main() {}
