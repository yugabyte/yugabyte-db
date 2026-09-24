use pgrx::array::FlatArray;
use pgrx::memcx::MemCx;
use pgrx::palloc::PBox;
use pgrx::prelude::*;

#[pg_extern]
fn something<'a>(_mcx: &MemCx<'a>) -> PBox<'a, FlatArray<'a, FlatArray<'a, i32>>> {
    todo!()
}

fn main() {}
