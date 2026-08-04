//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/// All top-level pgrx objects, **regardless** of the ".rs" file they're defined in, are created
/// in the schema determined by `CREATE EXTENSION`.  It could be `public` (the default), or a
/// user-specified schema. We have no idea what that is.
use pgrx::prelude::*;
use serde::{Deserialize, Serialize};

::pgrx::pg_module_magic!();

#[derive(PostgresType, Serialize, Deserialize)]
pub struct MyType(pub(crate) String);

#[pg_extern]
fn hello_default_schema() -> &'static str {
    "Hello from the schema where you installed this extension"
}

/// we can create our own schemas, which are just Rust `mod`s.  Anything defined in this module
/// will be created in a Postgres schema of the same name
#[pg_schema]
mod some_schema {
    use pgrx::prelude::*;
    use serde::{Deserialize, Serialize};

    #[derive(PostgresType, Serialize, Deserialize)]
    pub struct MySomeSchemaType(pub(crate) String);

    #[pg_extern]
    fn hello_some_schema() -> &'static str {
        "Hello from some_schema"
    }
}

/// we can also cheat and put pgrx objects in Postgres' `pg_catalog` schema,
/// which will make them available regardless of the active `search_path`, but
/// requires that the extension be created by a super-user
#[pg_schema]
mod pg_catalog {
    use pgrx::prelude::*;
    use serde::{Deserialize, Serialize};

    #[derive(PostgresType, Serialize, Deserialize)]
    pub struct MyPgCatalogType(pub(crate) String);
}

/// similarly, we can create objects in Postgres' `public` schema.  This will at least require the
/// proper permissions by the user calling `CREATE EXTENSION`
#[pg_schema]
mod public {
    use pgrx::prelude::*;

    #[pg_extern]
    pub fn hello_public() -> &'static str {
        "Hello from the public schema"
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::pg_catalog::MyPgCatalogType;
    use crate::some_schema::MySomeSchemaType;
    use crate::MyType;
    use pgrx::prelude::*;

    #[pg_test]
    fn test_hello_default_schema() {
        assert_eq!(
            Ok(Some("Hello from the schema where you installed this extension")),
            Spi::get_one::<&str>("SELECT hello_default_schema()")
        );
    }

    #[pg_test]
    fn test_my_type() -> Result<(), pgrx::spi::Error> {
        assert_eq!(
            "test",
            // we don't need to qualify "MyType" because whatever schema it was created in
            // is applied to the "search_path" of this test function
            Spi::get_one::<MyType>("SELECT '\"test\"'::MyType")?.expect("datum was null").0
        );
        Ok(())
    }

    #[pg_test]
    fn test_hello_some_schema() {
        assert_eq!(
            Ok(Some("Hello from some_schema")),
            // "hello_some_schema()" is in "some_schema", so it needs to be qualified
            Spi::get_one::<&str>("SELECT some_schema.hello_some_schema()")
        );
    }

    #[pg_test]
    fn test_my_some_schema_type() -> Result<(), spi::Error> {
        Spi::connect_mut(|c| {
            // "MySomeSchemaType" is in 'some_schema', so it needs to be discoverable
            c.update("SET search_path TO some_schema,public", None, &[])?;
            assert_eq!(
                String::from("test"),
                c.select("SELECT '\"test\"'::MySomeSchemaType", None, &[])?
                    .first()
                    .get_one::<MySomeSchemaType>()
                    .expect("get_one::<MySomeSchemaType>() failed")
                    .unwrap()
                    .0
            );
            Ok(())
        })
    }

    #[pg_test]
    fn test_my_pg_catalog_type() -> Result<(), pgrx::spi::Error> {
        assert_eq!(
            String::from("test"),
            Spi::get_one::<MyPgCatalogType>("SELECT '\"test\"'::MyPgCatalogType")?
                .expect("datum was null")
                .0
        );
        Ok(())
    }

    #[pg_test]
    fn test_hello_public() {
        assert_eq!(
            Ok(Some("Hello from the public schema")),
            Spi::get_one::<&str>("SELECT hello_public()")
        );
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
