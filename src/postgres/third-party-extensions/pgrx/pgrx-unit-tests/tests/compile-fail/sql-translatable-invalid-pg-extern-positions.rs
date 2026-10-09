use pgrx::prelude::*;

#[pg_extern]
fn bad_bare_u8(_value: u8) -> u8 {
    0
}

#[pg_extern]
fn bad_unit_arg(_value: ()) {}

#[pg_extern]
fn bad_result_arg(_value: Result<i32, std::io::Error>) {}

#[pg_extern]
fn bad_setof_arg(_value: SetOfIterator<'static, i32>) {}

#[pg_extern]
fn bad_table_arg(_value: TableIterator<'static, (name!(a, i32),)>) {}

fn main() {}
