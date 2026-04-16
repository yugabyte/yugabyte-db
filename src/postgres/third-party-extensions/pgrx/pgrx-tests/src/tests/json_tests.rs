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
use pgrx::{Json, JsonB};

#[pg_extern]
fn json_arg(json: Json) -> Json {
    json
}

#[pg_extern]
fn jsonb_arg(json: JsonB) -> JsonB {
    json
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;
    use pgrx::{Json, JsonB};

    #[pg_test]
    fn test_json() -> Result<(), pgrx::spi::Error> {
        use serde::{Deserialize, Serialize};

        #[derive(Serialize, Deserialize)]
        struct User {
            username: String,
            first_name: String,
            last_name: String,
        }

        let json = Spi::get_one::<Json>(
            r#"  SELECT '{"username": "blahblahblah", "first_name": "Blah", "last_name": "McBlahFace"}'::json;  "#,
        )?.expect("datum was null");

        let user: User =
            serde_json::from_value(json.0).expect("failed to parse json response from SPI");
        assert_eq!(user.username, "blahblahblah");
        assert_eq!(user.first_name, "Blah");
        assert_eq!(user.last_name, "McBlahFace");
        Ok(())
    }

    #[pg_test]
    fn test_jsonb() -> Result<(), pgrx::spi::Error> {
        use serde::{Deserialize, Serialize};

        #[derive(Serialize, Deserialize)]
        struct User {
            username: String,
            first_name: String,
            last_name: String,
        }

        let json = Spi::get_one::<JsonB>(
            r#"  SELECT '{"username": "blahblahblah", "first_name": "Blah", "last_name": "McBlahFace"}'::jsonb;  "#,
        )?.expect("datum was null");

        let user: User =
            serde_json::from_value(json.0).expect("failed to parse json response from SPI");
        assert_eq!(user.username, "blahblahblah");
        assert_eq!(user.first_name, "Blah");
        assert_eq!(user.last_name, "McBlahFace");
        Ok(())
    }

    #[pg_test]
    fn test_json_arg() -> Result<(), pgrx::spi::Error> {
        let json = Spi::get_one_with_args::<Json>(
            "SELECT json_arg($1);",
            &[Json(serde_json::json!({ "foo": "bar" })).into()],
        )?
        .expect("json was null");

        assert_eq!(json.0, serde_json::json!({ "foo": "bar" }));

        Ok(())
    }

    #[pg_test]
    fn test_jsonb_arg() -> Result<(), pgrx::spi::Error> {
        let json = Spi::get_one_with_args::<JsonB>(
            "SELECT jsonb_arg($1);",
            &[JsonB(serde_json::json!({ "foo": "bar" })).into()],
        )?
        .expect("json was null");

        assert_eq!(json.0, serde_json::json!({ "foo": "bar" }));

        Ok(())
    }
}
