//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#![allow(clippy::assign_op_pattern)]
use pgrx::prelude::*;

pg_module_magic!();

#[pg_extern]
fn add_numeric(a: Numeric<1000, 33>, b: Numeric<1000, 33>) -> Numeric<1000, 33> {
    (a + b).rescale().unwrap()
}

#[pg_extern]
fn random_numeric() -> AnyNumeric {
    rand::random::<i128>().into()
}

#[pg_extern]
fn numeric_from_string(s: &str) -> AnyNumeric {
    s.try_into().unwrap()
}

// select (((((5 * 5) - 2.234) / 4) % 19) + 99.42)::numeric(10, 3)
#[pg_extern]
fn math() -> Numeric<10, 3> {
    let mut n = AnyNumeric::from(5);

    n *= 5;
    n -= 2.234;
    n /= 4_i128;
    n %= 19;
    n += 99.42;

    let mut n: AnyNumeric = n * 42;
    n = n / 42;
    n = n + 42;
    n = n - 42;
    n = n * 42.0f32;
    n = n / 42.0f32;
    n = n + 42.0f32;
    n = n - 42.0f32;
    n = n * 42.0f64;
    n = n / 42.0f64;
    n = n + 42.0f64;
    n = n - 42.0f64;

    n = n / AnyNumeric::from(42);
    n = n / Numeric::<1000, 33>::try_from(42).unwrap();

    n.rescale().unwrap()
}

#[pg_extern]
fn forty_twooooooo() -> Numeric<1000, 33> {
    42.try_into().unwrap()
}
