use pgrx::memcx::MemCx;
use pgrx::palloc::PBox;
use pgrx::prelude::*;

#[pg_extern]
pub fn accept_mcx_return_timetz(memcx: &MemCx<'static>) -> PBox<'static, TimeWithTimeZone> {
    let timetz = TimeWithTimeZone::new(4, 20, 0.0).unwrap();
    PBox::new_in(timetz, memcx)
}

fn main() {}
