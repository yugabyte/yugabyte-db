//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;
    use pgrx::prelude::*;
    use pgrx::{AllocatedByPostgres, AllocatedByRust, WhoAllocated};
    use std::error::Error;

    /// Test some various formats of trigger signature we expect to support
    ///
    /// These tests don't run, but they get built to SQL and compile checked.
    #[pgrx::pg_schema]
    mod trigger_signature_compile_tests {
        use pgrx::heap_tuple::PgHeapTupleError;
        use pgrx::prelude::*;
        use pgrx::{AllocatedByPostgres, AllocatedByRust};

        use super::*;

        #[pg_trigger]
        fn signature_standard<'a>(
            trigger: &'a pgrx::PgTrigger<'a>,
        ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, Box<dyn Error>> {
            Ok(Some(trigger.old().unwrap().into_owned()))
        }

        #[pg_trigger]
        fn signature_explicit_lifetimes<'a>(
            trigger: &'a pgrx::PgTrigger<'a>,
        ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, Box<dyn Error>> {
            Ok(Some(trigger.old().unwrap().into_owned()))
        }

        #[pg_trigger]
        fn signature_alloc_by_postgres<'a>(
            trigger: &'a pgrx::PgTrigger<'a>,
        ) -> Result<Option<PgHeapTuple<'a, AllocatedByPostgres>>, Box<dyn Error>> {
            Ok(Some(trigger.old().unwrap()))
        }

        #[pg_trigger]
        fn signature_alloc_by_rust<'a>(
            trigger: &'a pgrx::PgTrigger<'a>,
        ) -> Result<Option<PgHeapTuple<'a, AllocatedByRust>>, Box<dyn Error>> {
            Ok(Some(trigger.old().unwrap().into_owned()))
        }

        // Check type aliases
        type AliasedBorrowedPgTrigger<'a> = &'a pgrx::PgTrigger<'a>;

        #[pg_trigger]
        fn signature_aliased_argument(
            trigger: AliasedBorrowedPgTrigger,
        ) -> Result<Option<PgHeapTuple<impl WhoAllocated>>, PgHeapTupleError> {
            Ok(Some(trigger.old().unwrap().into_owned()))
        }

        type AliasedTriggerResult<'a> =
            Result<Option<PgHeapTuple<'a, AllocatedByRust>>, TriggerError>;

        #[pg_trigger]
        fn signature_aliased_return<'a>(_trigger: &'a pgrx::PgTrigger) -> AliasedTriggerResult<'a> {
            unimplemented!("Only testing signature compiles")
        }

        #[pg_trigger]
        fn signature_aliased_both(_trigger: AliasedBorrowedPgTrigger) -> AliasedTriggerResult<'_> {
            unimplemented!("Only testing signature compiles")
        }
    }

    #[derive(thiserror::Error, Debug)]
    enum TriggerError {
        #[error("Null OLD found")]
        NullTriggerTuple,
        #[error("PgHeapTuple: {0}")]
        PgHeapTuple(#[from] pgrx::heap_tuple::PgHeapTupleError),
        #[error("TryFromDatumError: {0}")]
        TryFromDatum(#[from] pgrx::datum::TryFromDatumError),
        #[error("TryFromIntError: {0}")]
        TryFromInt(#[from] std::num::TryFromIntError),
        #[error("PgTrigger error: {0}")]
        PgTrigger(#[from] pgrx::trigger_support::PgTriggerError),
    }

    #[pg_trigger]
    fn field_species_fox_to_bear<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, TriggerError> {
        let mut new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?.into_owned();

        let field = "species";

        if new.get_by_name(field)? == Some("Fox") {
            new.set_by_name(field, "Bear")?;
        }

        Ok(Some(new))
    }

    #[pg_test]
    fn before_insert_field_update() {
        Spi::run(
            r#"
            CREATE TABLE tests.before_insert_field_update (species TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER foxes_to_bears
                BEFORE INSERT ON tests.before_insert_field_update
                FOR EACH ROW
                EXECUTE PROCEDURE tests.field_species_fox_to_bear()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.before_insert_field_update (species)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT species FROM tests.before_insert_field_update;");
        assert_eq!(retval, Ok(Some("Bear")));
    }

    #[pg_trigger]
    fn add_field_boopers<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, TriggerError> {
        let mut new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?.into_owned();

        let field = "booper";

        if new.get_by_name(field)? == Option::<&str>::None {
            new.set_by_name(field, "Swooper")?;
        }

        Ok(Some(new))
    }

    #[pg_test]
    fn before_insert_add_field() {
        Spi::run(
            r#"
            CREATE TABLE tests.before_insert_add_field (name TEXT, booper TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER add_field
                BEFORE INSERT ON tests.before_insert_add_field
                FOR EACH ROW
                EXECUTE PROCEDURE tests.add_field_boopers()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.before_insert_add_field (name)
                VALUES ('Nami')
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT booper FROM tests.before_insert_add_field;");
        assert_eq!(retval, Ok(Some("Swooper")));
    }

    #[pg_trigger]
    fn intercept_bears<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, TriggerError> {
        let new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?;

        for index in 1..(new.len() + 1) {
            if let Some(val) = new.get_by_index::<&str>(index.try_into()?)? {
                if val == "Bear" {
                    // We intercepted a bear! Avoid this update, return `old` instead.
                    let old = trigger.old().ok_or(TriggerError::NullTriggerTuple)?;
                    return Ok(Some(old));
                }
            }
        }

        Ok(Some(new))
    }

    #[pg_test]
    fn before_update_skip() {
        Spi::run(
            r#"
            CREATE TABLE tests.before_update_skip (title TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER add_field
                BEFORE UPDATE ON tests.before_update_skip
                FOR EACH ROW
                EXECUTE PROCEDURE tests.intercept_bears()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.before_update_skip (title)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            UPDATE tests.before_update_skip SET title = 'Bear'
                WHERE title = 'Fox'
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT title FROM tests.before_update_skip;");
        assert_eq!(retval, Ok(Some("Fox")));
    }

    #[pg_trigger]
    fn inserts_trigger_metadata<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, Box<dyn Error>> {
        let mut new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?.into_owned();

        let trigger_name = trigger.name()?;
        new.set_by_name("trigger_name", trigger_name)?;

        let trigger_when = trigger.when()?.to_string();
        new.set_by_name("trigger_when", trigger_when)?;

        let trigger_level = trigger.level().to_string();
        new.set_by_name("trigger_level", trigger_level)?;

        let trigger_op = trigger.op()?.to_string();
        new.set_by_name("trigger_op", trigger_op)?;

        let trigger_relid = trigger.relid()?;
        new.set_by_name("trigger_relid", trigger_relid)?;

        let trigger_old_transition_table_name = trigger.old_transition_table_name()?;
        new.set_by_name("trigger_old_transition_table_name", trigger_old_transition_table_name)?;

        let trigger_new_transition_table_name = trigger.new_transition_table_name()?;
        new.set_by_name("trigger_new_transition_table_name", trigger_new_transition_table_name)?;

        let trigger_table_name = trigger.table_name()?;
        new.set_by_name("trigger_table_name", trigger_table_name)?;

        let trigger_table_schema = trigger.table_schema()?;
        new.set_by_name("trigger_table_schema", trigger_table_schema)?;

        let trigger_extra_args = trigger.extra_args()?;
        new.set_by_name("trigger_extra_args", trigger_extra_args)?;

        Ok(Some(new))
    }

    #[pg_test]
    fn before_insert_metadata() -> Result<(), pgrx::spi::Error> {
        Spi::run(
            r#"
            CREATE TABLE tests.before_insert_trigger_metadata (
                marker TEXT,
                trigger_name TEXT,
                trigger_when TEXT,
                trigger_level TEXT,
                trigger_op TEXT,
                trigger_relid OID,
                trigger_old_transition_table_name TEXT,
                trigger_new_transition_table_name TEXT,
                trigger_table_name TEXT,
                trigger_table_schema TEXT,
                trigger_extra_args TEXT[]
            )
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER insert_trigger_metadata
                BEFORE INSERT ON tests.before_insert_trigger_metadata
                FOR EACH ROW
                EXECUTE PROCEDURE tests.inserts_trigger_metadata('Bears', 'Dogs')
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.before_insert_trigger_metadata (marker)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let marker =
            Spi::get_one::<&str>("SELECT marker FROM tests.before_insert_trigger_metadata;")?
                .unwrap();
        let trigger_name =
            Spi::get_one::<&str>("SELECT trigger_name FROM tests.before_insert_trigger_metadata;")?
                .unwrap();
        let trigger_when =
            Spi::get_one::<&str>("SELECT trigger_when FROM tests.before_insert_trigger_metadata;")?
                .unwrap();
        let trigger_level = Spi::get_one::<&str>(
            "SELECT trigger_level FROM tests.before_insert_trigger_metadata;",
        )?
        .unwrap();
        let trigger_op =
            Spi::get_one::<&str>("SELECT trigger_op FROM tests.before_insert_trigger_metadata;")?
                .unwrap();
        let trigger_relid = Spi::get_one::<pg_sys::Oid>(
            "SELECT trigger_relid FROM tests.before_insert_trigger_metadata;",
        )?;
        let trigger_old_transition_table_name = Spi::get_one::<&str>(
            "SELECT trigger_old_transition_table_name FROM tests.before_insert_trigger_metadata;",
        )?;
        let trigger_new_transition_table_name = Spi::get_one::<&str>(
            "SELECT trigger_new_transition_table_name FROM tests.before_insert_trigger_metadata;",
        )?;
        let trigger_table_name = Spi::get_one::<&str>(
            "SELECT trigger_table_name FROM tests.before_insert_trigger_metadata;",
        )?
        .unwrap();
        let trigger_table_schema = Spi::get_one::<&str>(
            "SELECT trigger_table_schema FROM tests.before_insert_trigger_metadata;",
        )?
        .unwrap();
        let trigger_extra_args = Spi::get_one::<Vec<String>>(
            "SELECT trigger_extra_args FROM tests.before_insert_trigger_metadata;",
        )?
        .unwrap();

        assert_eq!(marker, "Fox");
        assert_eq!(trigger_name, "insert_trigger_metadata");
        assert_eq!(trigger_when, "BEFORE");
        assert_eq!(trigger_level, "ROW");
        assert_eq!(trigger_op, "INSERT");
        assert!(trigger_relid.is_some());
        assert_eq!(trigger_old_transition_table_name, None);
        assert_eq!(trigger_new_transition_table_name, None);
        assert_eq!(trigger_table_name, "before_insert_trigger_metadata");
        assert_eq!(trigger_table_schema, "tests");
        assert_eq!(trigger_extra_args, vec!["Bears".to_string(), "Dogs".to_string()]);
        Ok(())
    }

    #[pg_trigger]
    fn inserts_trigger_metadata_safe<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, Box<dyn Error>> {
        let mut new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?.into_owned();

        new.set_by_name("trigger_name", trigger.name()?)?;
        new.set_by_name("trigger_when", trigger.when()?.to_string())?;
        new.set_by_name("trigger_level", trigger.level().to_string())?;
        new.set_by_name("trigger_op", trigger.op()?.to_string())?;
        new.set_by_name("trigger_relid", trigger.relid()?)?;
        new.set_by_name("trigger_old_transition_table_name", trigger.old_transition_table_name()?)?;
        new.set_by_name("trigger_new_transition_table_name", trigger.new_transition_table_name()?)?;
        new.set_by_name("trigger_table_name", trigger.relation()?.name())?;
        new.set_by_name("trigger_table_schema", trigger.relation()?.namespace())?;
        new.set_by_name("trigger_extra_args", trigger.extra_args()?)?;

        Ok(Some(new))
    }

    #[pg_test]
    fn before_insert_metadata_safe() -> Result<(), pgrx::spi::Error> {
        Spi::run(
            r#"
            CREATE TABLE tests.before_insert_trigger_metadata_safe (
                marker TEXT,
                trigger_name TEXT,
                trigger_when TEXT,
                trigger_level TEXT,
                trigger_op TEXT,
                trigger_relid OID,
                trigger_old_transition_table_name TEXT,
                trigger_new_transition_table_name TEXT,
                trigger_table_name TEXT,
                trigger_table_schema TEXT,
                trigger_extra_args TEXT[]
            )
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER insert_trigger_metadata_safe
                BEFORE INSERT ON tests.before_insert_trigger_metadata_safe
                FOR EACH ROW
                EXECUTE PROCEDURE tests.inserts_trigger_metadata_safe('Bears', 'Dogs')
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.before_insert_trigger_metadata_safe (marker)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let marker =
            Spi::get_one::<&str>("SELECT marker FROM tests.before_insert_trigger_metadata_safe;")?
                .unwrap();
        let trigger_name = Spi::get_one::<&str>(
            "SELECT trigger_name FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_when = Spi::get_one::<&str>(
            "SELECT trigger_when FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_level = Spi::get_one::<&str>(
            "SELECT trigger_level FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_op = Spi::get_one::<&str>(
            "SELECT trigger_op FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_relid = Spi::get_one::<pg_sys::Oid>(
            "SELECT trigger_relid FROM tests.before_insert_trigger_metadata_safe;",
        )?;
        let trigger_old_transition_table_name = Spi::get_one::<&str>(
            "SELECT trigger_old_transition_table_name FROM tests.before_insert_trigger_metadata_safe;",
        )?;
        let trigger_new_transition_table_name = Spi::get_one::<&str>(
            "SELECT trigger_new_transition_table_name FROM tests.before_insert_trigger_metadata_safe;",
        )?;
        let trigger_table_name = Spi::get_one::<&str>(
            "SELECT trigger_table_name FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_table_schema = Spi::get_one::<&str>(
            "SELECT trigger_table_schema FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();
        let trigger_extra_args = Spi::get_one::<Vec<String>>(
            "SELECT trigger_extra_args FROM tests.before_insert_trigger_metadata_safe;",
        )?
        .unwrap();

        assert_eq!(marker, "Fox");
        assert_eq!(trigger_name, "insert_trigger_metadata_safe");
        assert_eq!(trigger_when, "BEFORE");
        assert_eq!(trigger_level, "ROW");
        assert_eq!(trigger_op, "INSERT");
        assert!(trigger_relid.is_some());
        assert_eq!(trigger_old_transition_table_name, None);
        assert_eq!(trigger_new_transition_table_name, None);
        assert_eq!(trigger_table_name, "before_insert_trigger_metadata_safe");
        assert_eq!(trigger_table_schema, "tests");
        assert_eq!(trigger_extra_args, vec!["Bears".to_string(), "Dogs".to_string()]);
        Ok(())
    }

    #[pg_trigger(sql = r#"
        CREATE FUNCTION tests."has_sql_option_set_and_respects_it"()
        RETURNS TRIGGER
        LANGUAGE c
        AS 'MODULE_PATHNAME', '@FUNCTION_NAME@';
    "#)]
    fn has_sql_option_set<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, impl WhoAllocated>>, Box<dyn Error>> {
        let new = trigger.new().ok_or(TriggerError::NullTriggerTuple)?.into_owned();

        Ok(Some(new))
    }

    #[pg_test]
    fn before_insert_has_sql_option_set() {
        Spi::run(
            r#"
            CREATE TABLE tests.has_sql_option_set (species TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER has_sql_option_set
                BEFORE INSERT ON tests.has_sql_option_set
                FOR EACH ROW
                EXECUTE PROCEDURE tests.has_sql_option_set_and_respects_it()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.has_sql_option_set (species)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT species FROM tests.has_sql_option_set;");
        assert_eq!(retval, Ok(Some("Fox")));
    }

    #[pg_trigger]
    fn noop_postgres<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, AllocatedByPostgres>>, Box<dyn Error>> {
        Ok(Some(trigger.new().unwrap()))
    }

    #[pg_test]
    fn before_insert_noop_postgres() {
        Spi::run(
            r#"
            CREATE TABLE tests.has_noop_postgres (species TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER noop_postgres
                BEFORE INSERT ON tests.has_noop_postgres
                FOR EACH ROW
                EXECUTE PROCEDURE tests.noop_postgres()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.has_noop_postgres (species)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT species FROM tests.has_noop_postgres;");
        assert_eq!(retval, Ok(Some("Fox")));
    }

    #[pg_trigger]
    fn noop_rust<'a>(
        trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, AllocatedByRust>>, Box<dyn Error>> {
        Ok(Some(trigger.new().unwrap().into_owned()))
    }

    #[pg_test]
    fn before_insert_noop_rust() {
        Spi::run(
            r#"
            CREATE TABLE tests.has_noop_rust (species TEXT)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER noop_postgres
                BEFORE INSERT ON tests.has_noop_rust
                FOR EACH ROW
                EXECUTE PROCEDURE tests.noop_rust()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.has_noop_rust (species)
                VALUES ('Fox')
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<&str>("SELECT species FROM tests.has_noop_rust;");
        assert_eq!(retval, Ok(Some("Fox")));
    }

    #[pg_trigger]
    fn dont_delete_trigger<'a>(
        _trigger: &'a pgrx::PgTrigger<'a>,
    ) -> Result<Option<PgHeapTuple<'a, AllocatedByRust>>, Box<dyn Error>> {
        Ok(None)
    }

    #[pg_test]
    fn dont_delete() {
        Spi::run(
            r#"
            CREATE TABLE tests.dont_delete (id int)
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            CREATE TRIGGER dont_delete
                BEFORE DELETE ON tests.dont_delete
                FOR EACH ROW
                EXECUTE PROCEDURE tests.dont_delete_trigger()
        "#,
        )
        .expect("SPI failed");

        Spi::run(
            r#"
            INSERT INTO tests.dont_delete (id) VALUES ('1');
            DELETE FROM tests.dont_delete;
        "#,
        )
        .expect("SPI failed");

        let retval = Spi::get_one::<i32>("SELECT id FROM tests.dont_delete;");
        assert_eq!(retval, Ok(Some(1)));
    }
}
