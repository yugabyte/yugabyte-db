//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;

::pgrx::pg_module_magic!();

extension_sql!(
    r#"

CREATE TABLE spi_example (
    id serial8 not null primary key,
    title text
);

INSERT INTO spi_example (title) VALUES ('This is a test');
INSERT INTO spi_example (title) VALUES ('Hello There!');
INSERT INTO spi_example (title) VALUES ('I like pudding');


"#,
    name = "create_sqi_example_table",
);

#[pg_extern]
fn spi_return_query() -> Result<
    TableIterator<'static, (name!(oid, Option<pg_sys::Oid>), name!(name, Option<String>))>,
    spi::Error,
> {
    #[cfg(feature = "pg13")]
    let query = "SELECT oid, relname::text || '-pg13' FROM pg_class";
    #[cfg(feature = "pg14")]
    let query = "SELECT oid, relname::text || '-pg14' FROM pg_class";
    #[cfg(feature = "pg15")]
    let query = "SELECT oid, relname::text || '-pg15' FROM pg_class";
    #[cfg(feature = "pg16")]
    let query = "SELECT oid, relname::text || '-pg16' FROM pg_class";
    #[cfg(feature = "pg17")]
    let query = "SELECT oid, relname::text || '-pg17' FROM pg_class";

    Spi::connect(|client| {
        client
            .select(query, None, &[])?
            .map(|row| Ok((row["oid"].value()?, row[2].value()?)))
            .collect::<Result<Vec<_>, _>>()
    })
    .map(TableIterator::new)
}

#[pg_extern(immutable, parallel_safe)]
fn spi_query_random_id() -> Result<Option<i64>, pgrx::spi::Error> {
    Spi::get_one("SELECT id FROM spi.spi_example ORDER BY random() LIMIT 1")
}

#[pg_extern]
fn spi_query_title(title: &str) -> Result<Option<i64>, pgrx::spi::Error> {
    Spi::get_one_with_args("SELECT id FROM spi.spi_example WHERE title = $1;", &[title.into()])
}

#[pg_extern]
fn spi_query_by_id(id: i64) -> Result<Option<String>, spi::Error> {
    let (returned_id, title) = Spi::connect(|client| {
        let tuptable = client
            .select("SELECT id, title FROM spi.spi_example WHERE id = $1", None, &[id.into()])?
            .first();

        tuptable.get_two::<i64, String>()
    })?;

    info!("id={:?}", returned_id);
    Ok(title)
}

#[pg_extern]
fn spi_insert_title(title: &str) -> Result<Option<i64>, spi::Error> {
    Spi::get_one_with_args(
        "INSERT INTO spi.spi_example(title) VALUES ($1) RETURNING id",
        &[title.into()],
    )
}

#[pg_extern]
fn spi_insert_title2(
    title: &str,
) -> TableIterator<(name!(id, Option<i64>), name!(title, Option<String>))> {
    let tuple = Spi::get_two_with_args(
        "INSERT INTO spi.spi_example(title) VALUES ($1) RETURNING id, title",
        &[title.into()],
    )
    .unwrap();

    TableIterator::once(tuple)
}

#[pg_extern]
fn issue1209_fixed() -> Result<Option<String>, Box<dyn std::error::Error>> {
    let res = Spi::connect(|c| {
        let mut cursor = c.try_open_cursor("SELECT 'hello' FROM generate_series(1, 10000)", &[])?;
        let table = cursor.fetch(10000)?;
        table.into_iter().map(|row| row.get::<&str>(1)).collect::<Result<Vec<_>, _>>()
    })?;

    Ok(res.first().cloned().flatten().map(|s| s.to_string()))
}

extension_sql!(
    r#"

CREATE TABLE foo ();

"#,
    name = "create_foo_table"
);

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::spi_query_by_id;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_spi_query_by_id_direct() {
        assert_eq!(Ok(Some("This is a test".to_string())), spi_query_by_id(1));
    }

    #[pg_test]
    fn test_spi_query_by_id_via_spi() {
        let result = Spi::get_one::<&str>("SELECT spi.spi_query_by_id(1)");

        assert_eq!(Ok(Some("This is a test")), result);
    }
}

#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}
