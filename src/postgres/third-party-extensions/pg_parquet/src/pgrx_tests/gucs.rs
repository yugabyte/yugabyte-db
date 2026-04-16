#[pgrx::pg_schema]
mod tests {
    use pgrx::{pg_test, Spi};

    use crate::pgrx_tests::common::LOCAL_TEST_FILE_PATH;

    #[pg_test]
    #[should_panic(expected = "EOF: file size of 2 is less than footer")]
    fn test_disabled_hooks() {
        Spi::run("SET pg_parquet.enable_copy_hooks TO false;").unwrap();
        Spi::run(format!("COPY (SELECT 1 as id) TO '{}'", LOCAL_TEST_FILE_PATH).as_str()).unwrap();

        let parquet_metadata_command = format!(
            "select * from parquet.metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&parquet_metadata_command).unwrap();
    }
}
