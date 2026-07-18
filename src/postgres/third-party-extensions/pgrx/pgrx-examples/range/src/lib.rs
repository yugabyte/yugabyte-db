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
fn range(s: i32, e: i32) -> pgrx::Range<i32> {
    (s..e).into()
}

#[pg_extern]
fn range_from(s: i32) -> pgrx::Range<i32> {
    (s..).into()
}

#[pg_extern]
fn range_full() -> pgrx::Range<i32> {
    (..).into()
}

#[pg_extern]
fn range_inclusive(s: i32, e: i32) -> pgrx::Range<i32> {
    (s..=e).into()
}

#[pg_extern]
fn range_to(e: i32) -> pgrx::Range<i32> {
    (..e).into()
}

#[pg_extern]
fn range_to_inclusive(e: i32) -> pgrx::Range<i32> {
    (..=e).into()
}

#[pg_extern]
fn empty() -> pgrx::Range<i32> {
    pgrx::Range::empty()
}

#[pg_extern]
fn infinite() -> pgrx::Range<i32> {
    pgrx::Range::infinite()
}

#[pg_extern]
fn assert_range(r: pgrx::Range<i32>, s: i32, e: i32) -> bool {
    r == (s..e).into()
}
