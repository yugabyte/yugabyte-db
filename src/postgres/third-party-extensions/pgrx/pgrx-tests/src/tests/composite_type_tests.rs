use pgrx::prelude::*;

extension_sql!(
    r#"
    CREATE TYPE entity AS (id INT, lang TEXT);
    CREATE TYPE result AS (score DOUBLE PRECISION, entities entity[]);
    "#,
    name = "issue1293"
);

#[pg_extern(requires = ["issue1293"])]
fn create_result() -> pgrx::composite_type!('static, "result") {
    let mut record = PgHeapTuple::new_composite_type("result").unwrap();
    record.set_by_name("score", 0.707).unwrap();
    let mut entity_records: Vec<pgrx::composite_type!("entity")> = Vec::new();
    for i in 0..10 {
        let mut entity_record = PgHeapTuple::new_composite_type("entity").unwrap();
        entity_record.set_by_name("id", i).unwrap();
        entity_record.set_by_name("lang", "en".to_string()).unwrap();
        entity_records.push(entity_record);
    }
    record.set_by_name("entities", entity_records).unwrap();
    return record;
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[allow(unused_imports)]
    use crate as pgrx_tests;

    #[pg_test]
    fn test_array_of_composite_type() {
        Spi::get_one::<bool>("SELECT create_result() is not null")
            .expect("SPI failed")
            .expect("failed to create `result` composite type'");
    }
}
