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
use serde::{Deserialize, Serialize};

::pgrx::pg_module_magic!();

#[pg_schema]
mod home {
    use super::*;

    #[pg_schema]
    pub mod dogs {
        use super::*;

        #[derive(PostgresEnum, Serialize, Deserialize)]
        pub enum Dog {
            Brandy,
            Nami,
        }
    }

    #[derive(PostgresType, Serialize, Deserialize)]
    pub struct Ball {
        last_chomp: Dog,
    }
}
pub use home::dogs::Dog;

// `extension_sql` allows you to define your own custom SQL.
//
// Since PostgreSQL is is order dependent, you may need to specify a positioning.
//
// Valid options are:
//  * `bootstrap` positions the block before any other generated SQL. It should be unique.
//     Errors if `before`/`after` are also present.
//  * `before = [$ident]` & `after = [$ident]` positions the block before/after `$ident`
//    where `$ident` is a string identifier or a path to a SQL entity (such as a type which derives
//    `PostgresType`)
//  * `creates = [Enum($ident), Type($ident), Function($ident)]` tells the dependency graph that this block creates a given entity.
//  * `name` is an optional string identifier for the item, in case you need to refer to it in
//    other positioning.
extension_sql!(
    "\n\
        CREATE TABLE extension_sql (message TEXT);\n\
        INSERT INTO extension_sql VALUES ('bootstrap');\n\
    ",
    name = "bootstrap_raw",
    bootstrap,
);
extension_sql!(
    "\n
        INSERT INTO extension_sql VALUES ('single_raw');\n\
    ",
    name = "single_raw",
    requires = [home::dogs]
);
extension_sql!(
    "\n\
    INSERT INTO extension_sql VALUES ('multiple_raw');\n\
",
    name = "multiple_raw",
    requires = [Dog, home::Ball, "single_raw", "single"],
);

// `extension_sql_file` does the same as `extension_sql` but automatically sets the `name` to the
// filename (not the full path).
extension_sql_file!("../sql/single.sql", requires = ["single_raw"]);
extension_sql_file!(
    "../sql/multiple.sql",
    requires = [Dog, home::Ball, "single_raw", "single", "multiple_raw"],
);
extension_sql_file!("../sql/finalizer.sql", finalize);

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_ordering() -> Result<(), spi::Error> {
        let buf = Spi::connect(|client| {
            Ok::<_, spi::Error>(
                client
                    .select("SELECT * FROM extension_sql", None, &[])?
                    .flat_map(|tup| {
                        tup.get_datum_by_ordinal(1)
                            .ok()
                            .and_then(|ord| ord.value::<String>().ok().unwrap())
                    })
                    .collect::<Vec<String>>(),
            )
        })?;

        assert_eq!(
            buf,
            vec![
                String::from("bootstrap"),
                String::from("single_raw"),
                String::from("single"),
                String::from("multiple_raw"),
                String::from("multiple"),
                String::from("finalizer")
            ]
        );
        Ok(())
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
