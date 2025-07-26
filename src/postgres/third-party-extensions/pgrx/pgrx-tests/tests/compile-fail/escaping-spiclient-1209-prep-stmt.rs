use pgrx::prelude::*;
use std::error::Error;

#[pg_extern]
fn issue1209_prepared_stmt(q: &str) -> Result<Option<String>, Box<dyn Error>> {
    use pgrx::spi::Query;

    let prepared = { Spi::connect(|c| c.prepare(q, &[]))? };

    Ok(Spi::connect(|c| prepared.execute(&c, Some(1), &[])?.first().get(1))?)
}

fn main() {}
