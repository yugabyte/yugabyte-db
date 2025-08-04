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

#[derive(PostgresType, Serialize, Deserialize)]
#[serde(untagged)]
pub enum SomeEnum {
    String(String),
    Struct { a: usize, s: String },
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::rust_enum::SomeEnum;
    use pgrx::prelude::*;

    #[cfg(not(feature = "no-schema-generation"))]
    #[pg_test]
    fn test_some_enum() {
        let val = Spi::get_one::<SomeEnum>(r#"SELECT '"hello world"'::SomeEnum"#);

        assert!(matches!(
            val,
            Ok(Some(SomeEnum::String(s))) if s == "hello world"
        ));

        let val = Spi::get_one::<SomeEnum>(r#"SELECT '{"a": 1, "s": "hello world"}'::SomeEnum"#);

        assert!(matches!(
            val,
            Ok(Some(SomeEnum::Struct{a: 1, s })) if s == "hello world"
        ));
    }
}
