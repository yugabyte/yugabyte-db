use pgrx::prelude::*;

fn main() {}

#[pg_extern]
pub fn cast_function() {
    Spi::connect(|client| {
        let stmt = client.prepare(c"SELECT 1", None)?;

        client.prepare(stmt, None);

        Ok(())
    });
}
