use pgrx::prelude::*;

#[pg_extern]
fn returns_tuple_with_lifetime(
    value: &'static str,
) -> TableIterator<(name!(a, &'static str), name!(b, Option<&'static str>))> {
    TableIterator::once((value, Some(value)))
}

fn main() {}
