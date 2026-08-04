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

::pgrx::pg_module_magic!();

#[pg_extern]
fn sq_euclid_pgrx(a: Array<f32>, b: Array<f32>) -> f32 {
    a.iter_deny_null().zip(b.iter_deny_null()).map(|(a, b)| (a - b) * (a - b)).sum()
}

#[pg_extern(immutable, parallel_safe)]
fn approx_distance_pgrx(compressed: Array<i64>, distances: Array<f64>) -> f64 {
    compressed
        .iter_deny_null()
        .map(|cc| {
            let d = distances.get(cc as usize).unwrap().unwrap();
            pgrx::info!("cc={cc}, d={d}");
            d
        })
        .sum()
}

#[pg_extern]
fn default_array() -> Vec<i32> {
    Default::default()
}

#[pg_extern(requires = [ default_array, ])]
fn sum_array(input: default!(Array<i32>, "default_array()")) -> i64 {
    let mut sum = 0_i64;

    for i in input {
        sum += i.unwrap_or(-1) as i64;
    }

    sum
}

#[pg_extern]
fn sum_vec(mut input: Vec<Option<i32>>) -> i64 {
    let mut sum = 0_i64;

    input.push(Some(6));

    for i in input {
        sum += i.unwrap_or_default() as i64;
    }

    sum
}

#[pg_extern]
fn static_names() -> Vec<Option<&'static str>> {
    vec![Some("Brandy"), Some("Sally"), None, Some("Anchovy")]
}

#[pg_extern]
fn static_names_set() -> SetOfIterator<'static, Vec<Option<&'static str>>> {
    SetOfIterator::new(vec![
        vec![Some("Brandy"), Some("Sally"), None, Some("Anchovy")],
        vec![Some("Eric"), Some("David")],
        vec![Some("ZomboDB"), Some("PostgreSQL"), Some("Elasticsearch")],
    ])
}

#[pg_extern]
fn i32_array_no_nulls() -> Vec<i32> {
    vec![1, 2, 3, 4, 5]
}

#[pg_extern]
fn i32_array_with_nulls() -> Vec<Option<i32>> {
    vec![Some(1), None, Some(2), Some(3), None, Some(4), Some(5)]
}

#[pg_extern]
fn strip_nulls(input: Vec<Option<i32>>) -> Vec<i32> {
    input.into_iter().flatten().collect()
}

#[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
pub struct SomeStruct {}

#[pg_extern]
#[search_path(@extschema@)]
fn return_vec_of_customtype() -> Vec<SomeStruct> {
    vec![SomeStruct {}]
}

#[pg_schema]
mod vectors {
    use pgrx::prelude::*;

    extension_sql!(
        r#"
            do $$ begin raise warning 'creating sample data in the table ''vectors.data''.'; end; $$;
            create table vectors.data as select vectors.random_vector(768) v from generate_series(1, 1000);
        "#,
        name = "vectors_data",
        requires = [random_vector]
    );

    #[pg_extern]
    fn sum_vector_array(input: Array<f32>) -> f32 {
        input.iter_deny_null().sum()
    }

    #[pg_extern]
    fn sum_vector_vec(input: Vec<f32>) -> f32 {
        input.into_iter().sum()
    }

    #[pg_extern]
    fn sum_vector_slice(input: Array<f32>) -> Result<f32, ArraySliceError> {
        Ok(input.as_slice()?.iter().copied().sum())
    }

    #[pg_extern]
    pub fn sum_vector_simd(values: Array<f32>) -> Result<f32, ArraySliceError> {
        // this comes directly from https://stackoverflow.com/questions/23100534/how-to-sum-the-values-in-an-array-slice-or-vec-in-rust/67191480#67191480
        // and https://www.reddit.com/r/rust/comments/13q56bp/comment/jlgq2t6/?utm_source=share&utm_medium=web2x&context=3
        //
        // the expectation here is that the compiler will autovectorize this code using SIMD
        use std::convert::TryInto;

        const LANES: usize = 16;

        let values = values.as_slice()?;
        let chunks = values.chunks_exact(LANES);
        let remainder = chunks.remainder();

        let sum = chunks.fold([0.0f32; LANES], |mut acc, chunk| {
            let chunk: [f32; LANES] = chunk.try_into().unwrap();
            for i in 0..LANES {
                acc[i] += chunk[i];
            }
            acc
        });

        let remainder: f32 = remainder.iter().copied().sum();

        let mut reduced = 0.0f32;
        for i in 0..LANES {
            reduced += sum[i];
        }
        Ok(reduced + remainder)
    }

    #[pg_extern]
    fn random_vector(len: i32) -> Vec<f32> {
        (0..len).map(|_| rand::random()).collect()
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

#[pg_schema]
#[cfg(any(test, feature = "pg_test"))]
pub mod tests {
    use crate::SomeStruct;
    use pgrx::prelude::*;

    #[pg_test]
    #[search_path(@extschema@)]
    fn test_vec_of_customtype() {
        let customvec =
            Spi::get_one::<Vec<SomeStruct>>("SELECT arrays.return_vec_of_customtype();");
        assert_eq!(customvec, Ok(Some(vec![SomeStruct {}])));
    }
}
