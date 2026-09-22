use pgrx::prelude::*;

struct MissingSqlType;

#[pg_extern]
fn takes_optional_missing_sql(_value: Option<MissingSqlType>) {}

#[pg_extern]
fn returns_result_missing_sql() -> Result<MissingSqlType, std::io::Error> {
    todo!()
}

#[pg_extern]
fn returns_table_with_missing_sql() -> TableIterator<'static, (name!(missing, MissingSqlType),)> {
    todo!()
}

fn main() {}
