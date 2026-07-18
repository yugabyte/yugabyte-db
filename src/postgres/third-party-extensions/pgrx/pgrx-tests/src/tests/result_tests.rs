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
use serde::*;

//
// the ResultTestsA type and the corresponding function test issue #1076.  Should that regress
// it's likely that `CREATE EXTENSION` will fail with:
//
//   ERROR SQLSTATE[42710]: type "resulttestsa" already exists
//

#[derive(Debug, Serialize, Deserialize, PostgresType)]
pub struct ResultTestsA;

#[pg_extern]
fn result_tests_a_func() -> Result<ResultTestsA, spi::Error> {
    Ok(ResultTestsA)
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use pgrx::pg_sys::panic::ErrorReport;
    use pgrx::prelude::*;
    use std::convert::Infallible;

    #[pg_extern]
    fn return_result() -> Result<i32, Infallible> {
        Ok(42)
    }

    #[pg_extern]
    fn return_some_optional_result() -> Result<Option<i32>, Infallible> {
        Ok(Some(42))
    }

    #[pg_extern]
    fn return_none_optional_result() -> Result<Option<i32>, Infallible> {
        Ok(None)
    }

    #[pg_extern]
    fn return_some_error() -> Result<i32, Box<dyn std::error::Error>> {
        Err("error!".into())
    }

    #[pg_extern]
    fn return_eyre_result() -> eyre::Result<i32> {
        Ok(42)
    }

    #[pg_extern]
    fn return_eyre_result_error() -> eyre::Result<i32> {
        Err(eyre::eyre!("error!"))
    }

    #[pg_extern]
    fn return_error_report() -> Result<(), ErrorReport> {
        Err(ErrorReport::new(
            PgSqlErrorCode::ERRCODE_NO_DATA,
            "raised custom ereport",
            function_name!(),
        ))
    }

    #[pg_extern]
    fn return_result_table_iterator(
    ) -> Result<TableIterator<'static, (name!(a, i32), name!(b, i32))>, pgrx::spi::Error> {
        Ok(TableIterator::once((1, 2)))
    }

    #[pg_extern]
    fn return_result_table_iterator_error(
    ) -> Result<TableIterator<'static, (name!(a, i32), name!(b, i32))>, pgrx::spi::Error> {
        Err(pgrx::spi::Error::InvalidPosition)
    }

    #[pg_extern]
    fn return_result_set_of() -> Result<SetOfIterator<'static, i32>, pgrx::spi::Error> {
        Ok(SetOfIterator::once(1))
    }

    #[pg_extern]
    fn return_result_set_of_error() -> Result<SetOfIterator<'static, i32>, pgrx::spi::Error> {
        Err(pgrx::spi::Error::InvalidPosition)
    }

    #[pg_test(error = "I am a platform-independent and locale-agnostic string")]
    fn test_return_io_error() -> Result<(), std::io::Error> {
        use std::io::{Error, ErrorKind};
        Err(Error::new(
            ErrorKind::NotFound,
            "I am a platform-independent and locale-agnostic string",
        ))
    }

    #[pg_test]
    fn test_return_result() {
        Spi::get_one::<i32>("SELECT tests.return_result()").expect("SPI returned NULL");
    }

    #[pg_test]
    fn test_return_some_optional_result() {
        assert_eq!(Ok(Some(42)), Spi::get_one::<i32>("SELECT tests.return_some_optional_result()"));
    }

    #[pg_test]
    fn test_return_none_optional_result() {
        assert_eq!(Ok(None), Spi::get_one::<i32>("SELECT tests.return_none_optional_result()"));
    }

    #[pg_test(error = "error!")]
    fn test_return_some_error() {
        Spi::get_one::<i32>("SELECT tests.return_some_error()").expect("SPI returned NULL");
    }

    #[pg_test]
    fn test_return_eyre_result() {
        Spi::get_one::<i32>("SELECT tests.return_eyre_result()").expect("SPI returned NULL");
    }

    #[pg_test(error = "error!")]
    fn test_return_eyre_result_error() {
        Spi::get_one::<i32>("SELECT tests.return_eyre_result_error()").expect("SPI returned NULL");
    }

    #[pg_test(error = "got proper sql errorcode")]
    fn test_proper_sql_errcode() -> Result<Option<i32>, pgrx::spi::Error> {
        PgTryBuilder::new(|| Spi::get_one::<i32>("SELECT tests.return_eyre_result_error()"))
            .catch_when(PgSqlErrorCode::ERRCODE_DATA_EXCEPTION, |_| {
                panic!("got proper sql errorcode")
            })
            .execute()
    }

    #[pg_test(error = "got proper sql errorcode")]
    fn test_proper_sql_errcode_from_error_report() -> Result<Option<i32>, pgrx::spi::Error> {
        PgTryBuilder::new(|| Spi::get_one::<i32>("SELECT tests.return_error_report()"))
            .catch_when(PgSqlErrorCode::ERRCODE_NO_DATA, |_| panic!("got proper sql errorcode"))
            .execute()
    }

    #[pg_test(error = "raised custom ereport")]
    fn test_custom_ereport() -> Result<(), ErrorReport> {
        Err(ErrorReport::new(
            PgSqlErrorCode::ERRCODE_NO_DATA,
            "raised custom ereport",
            function_name!(),
        ))
    }

    #[pg_test]
    fn test_return_result_table_iterator() -> Result<(), spi::Error> {
        Spi::run("SELECT * FROM tests.return_result_table_iterator()")
    }

    #[pg_test(error = "SpiTupleTable positioned before the start or after the end")]
    fn test_return_result_table_iterator_error() -> Result<(), spi::Error> {
        Spi::run("SELECT * FROM tests.return_result_table_iterator_error()")
    }

    #[pg_test]
    fn test_return_result_set_of() -> Result<(), spi::Error> {
        Spi::run("SELECT * FROM tests.return_result_set_of()")
    }

    #[pg_test(error = "SpiTupleTable positioned before the start or after the end")]
    fn test_return_result_set_of_error() -> Result<(), spi::Error> {
        Spi::run("SELECT * FROM tests.return_result_set_of_error()")
    }
}
