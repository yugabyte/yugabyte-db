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

#[derive(PostgresEnum, PartialEq, Debug, Default)]
pub enum Foo {
    #[default]
    One,
    Two,
    Three,
}

#[pg_extern]
fn take_foo_enum(value: Foo) -> Foo {
    assert_eq!(value, Foo::One);

    Foo::Three
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use super::Foo;
    use pgrx::prelude::*;

    #[test]
    fn make_idea_happy() {}

    #[pg_test]
    fn test_foo_enum() {
        let result = Spi::get_one::<Foo>("SELECT take_foo_enum('One');");
        assert_eq!(Ok(Some(Foo::Three)), result);
    }
}
