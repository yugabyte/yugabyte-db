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

#[pgrx::pg_schema]
mod test_schema {
    use pgrx::pgrx_sql_entity_graph::{PgrxSql, SqlGraphEntity};
    use pgrx::prelude::*;
    use serde::{Deserialize, Serialize};

    #[pg_extern]
    fn func_in_diff_schema() {}

    #[pg_extern(sql = false)]
    fn func_elided_from_schema() {}

    #[pg_extern(sql = generate_function)]
    fn func_generated_with_custom_sql() {}

    #[derive(Debug, PostgresType, Serialize, Deserialize)]
    pub struct TestType(pub u64);

    #[derive(Debug, PostgresType, Serialize, Deserialize)]
    #[pgrx(sql = false)]
    pub struct ElidedType(pub u64);

    #[derive(Debug, PostgresType, Serialize, Deserialize)]
    #[pgrx(sql = generate_type)]
    pub struct OtherType(pub u64);

    #[derive(Debug, PostgresType, Serialize, Deserialize)]
    #[pgrx(sql = "CREATE TYPE test_schema.ManuallyRenderedType;")]
    pub struct OverriddenType(pub u64);

    fn generate_function(
        entity: &SqlGraphEntity,
        _context: &PgrxSql,
    ) -> Result<String, Box<dyn std::error::Error + Send + Sync + 'static>> {
        if let SqlGraphEntity::Function(ref func) = entity {
            Ok(format!(
                "\
                CREATE FUNCTION test_schema.\"func_generated_with_custom_name\"() RETURNS void\n\
                LANGUAGE c /* Rust */\n\
                AS 'MODULE_PATHNAME', '{unaliased_name}_wrapper';\
                ",
                unaliased_name = func.name,
            ))
        } else {
            panic!("expected extern function entity, got {entity:?}");
        }
    }

    fn generate_type(
        entity: &SqlGraphEntity,
        _context: &PgrxSql,
    ) -> Result<String, Box<dyn std::error::Error + Send + Sync + 'static>> {
        if let SqlGraphEntity::Type(ref ty) = entity {
            Ok(format!(
                "\n\
                CREATE TYPE test_schema.Custom{name};\
                ",
                name = ty.name,
            ))
        } else {
            panic!("expected type entity, got {entity:?}");
        }
    }
}

#[pg_extern(schema = "test_schema")]
fn func_in_diff_schema2() {}

#[pg_extern]
fn type_in_diff_schema() -> test_schema::TestType {
    test_schema::TestType(1)
}

#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::prelude::*;

    #[pg_test]
    fn test_in_different_schema() {
        Spi::run("SELECT test_schema.func_in_diff_schema();").expect("SPI failed");
    }

    #[pg_test]
    fn test_in_different_schema2() {
        Spi::run("SELECT test_schema.func_in_diff_schema2();").expect("SPI failed");
    }

    #[pg_test]
    fn test_type_in_different_schema() {
        Spi::run("SELECT type_in_diff_schema();").expect("SPI failed");
    }

    #[pg_test]
    fn elided_extern_is_elided() {
        // Validate that a function we know exists, exists
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_proc WHERE proname = 'func_in_diff_schema');",
        );
        assert_eq!(result, Ok(Some(true)));

        // Validate that the function we expect not to exist, doesn't
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_proc WHERE proname = 'func_elided_from_schema');",
        );
        assert_eq!(result, Ok(Some(false)));
    }

    #[pg_test]
    fn elided_type_is_elided() {
        // Validate that a type we know exists, exists
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_type WHERE typname = 'testtype');",
        );
        assert_eq!(result, Ok(Some(true)));

        // Validate that the type we expect not to exist, doesn't
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_type WHERE typname = 'elidedtype');",
        );
        assert_eq!(result, Ok(Some(false)));
    }

    #[pg_test]
    fn custom_to_sql_extern() {
        // Validate that the function we generated has the modifications we expect
        let result = Spi::get_one::<bool>("SELECT exists(SELECT 1 FROM pg_proc WHERE proname = 'func_generated_with_custom_name');");
        assert_eq!(result, Ok(Some(true)));

        Spi::run("SELECT test_schema.func_generated_with_custom_name();").expect("SPI failed");
    }

    #[pg_test]
    fn custom_to_sql_type() {
        // Validate that the type we generated has the expected modifications
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_type WHERE typname = 'customothertype');",
        );
        assert_eq!(result, Ok(Some(true)));
    }

    #[pg_test]
    fn custom_handwritten_to_sql_type() {
        // Validate that the SQL we provided was used
        let result = Spi::get_one::<bool>(
            "SELECT exists(SELECT 1 FROM pg_type WHERE typname = 'manuallyrenderedtype');",
        );
        assert_eq!(result, Ok(Some(true)));
    }
}
