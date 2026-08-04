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

#[pg_extern]
fn generate_series(start: i64, finish: i64, step: default!(i64, 1)) -> SetOfIterator<'static, i64> {
    SetOfIterator::new((start..=finish).step_by(step as usize))
}

#[pg_extern]
fn generate_series_table(
    start: i64,
    finish: i64,
    step: default!(i64, 1),
) -> TableIterator<'static, (name!(id, i64), name!(val, i64))> {
    TableIterator::new(
        (start..=finish).step_by(step as usize).enumerate().map(|(idx, val)| (idx as _, val)),
    )
}

#[pg_extern]
fn random_values(num_rows: i32) -> TableIterator<'static, (name!(index, i32), name!(value, f64))> {
    TableIterator::new((1..=num_rows).map(|i| (i, rand::random::<f64>())))
}

#[pg_extern]
fn result_table() -> Result<
    ::pgrx::iter::TableIterator<'static, (name!(a, Option<i32>), name!(b, Option<i32>))>,
    Box<dyn std::error::Error + Send + Sync + 'static>,
> {
    Ok(TableIterator::new(vec![(Some(1), Some(2))]))
}

#[pg_extern]
fn one_col() -> TableIterator<'static, (name!(a, Option<i32>),)> {
    TableIterator::once((Some(42),))
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_it() {
        // do testing here.
        //
        // #[pg_test] functions run *inside* Postgres and have access to all Postgres internals
        //
        // Normal #[test] functions do not
        //
        // In either case, they all run in parallel
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
