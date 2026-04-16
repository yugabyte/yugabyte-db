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
    use pgrx::datum::DatumWithOid;
    use pgrx::Uuid;
    use std::error::Error;

    use pgrx::prelude::*;
    use pgrx::spi::{self, Query};

    #[pg_test(error = "syntax error at or near \"THIS\"")]
    fn test_spi_failure() -> Result<(), spi::Error> {
        Spi::connect(|client| client.select("THIS IS NOT A VALID QUERY", None, &[]).map(|_| ()))
    }

    #[pg_test]
    fn test_spi_can_nest() -> Result<(), spi::Error> {
        Spi::connect(|_| {
            Spi::connect(|_| Spi::connect(|_| Spi::connect(|_| Spi::connect(|_| Ok(())))))
        })
    }

    #[pg_test]
    fn test_spi_returns_primitive() -> Result<(), spi::Error> {
        let rc =
            Spi::connect(|client| client.select("SELECT 42", None, &[])?.first().get::<i32>(1))?;

        assert_eq!(Some(42), rc);
        Ok(())
    }

    #[pg_test]
    fn test_spi_returns_str() -> Result<(), spi::Error> {
        let rc = Spi::connect(|client| {
            client.select("SELECT 'this is a test'", None, &[])?.first().get::<&str>(1)
        })?;

        assert_eq!(Some("this is a test"), rc);
        Ok(())
    }

    #[pg_test]
    fn test_spi_returns_string() -> Result<(), spi::Error> {
        let rc = Spi::connect(|client| {
            client.select("SELECT 'this is a test'", None, &[])?.first().get::<&str>(1)
        })?;

        assert_eq!(Some("this is a test"), rc);
        Ok(())
    }

    #[pg_test]
    fn test_spi_get_one() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let i = client.select("SELECT 42::bigint", None, &[])?.first().get_one::<i64>()?;
            assert_eq!(Some(42), i);
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_get_two() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let (i, s) =
                client.select("SELECT 42, 'test'", None, &[])?.first().get_two::<i64, &str>()?;

            assert_eq!(Some(42), i);
            assert_eq!(Some("test"), s);
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_get_three() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let (i, s, b) = client
                .select("SELECT 42, 'test', true", None, &[])?
                .first()
                .get_three::<i64, &str, bool>()?;

            assert_eq!(Some(42), i);
            assert_eq!(Some("test"), s);
            assert_eq!(Some(true), b);
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_get_two_with_failure() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            assert!(client.select("SELECT 42", None, &[])?.first().get_two::<i64, &str>().is_err());
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_get_three_failure() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            assert!(client
                .select("SELECT 42, 'test'", None, &[])?
                .first()
                .get_three::<i64, &str, bool>()
                .is_err());
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_select_zero_rows() {
        assert!(Spi::get_one::<i32>("SELECT 1 LIMIT 0").is_err());
    }

    #[pg_test]
    fn test_spi_run() {
        assert!(Spi::run("SELECT 1").is_ok());
    }

    #[pg_test]
    fn test_spi_run_with_args() {
        let i = 1 as i32;
        let j = 2 as i64;

        assert!(Spi::run_with_args("SELECT $1 + $2 = 3", &[i.into(), j.into(),],).is_ok());
    }

    #[pg_test]
    fn test_spi_explain() -> Result<(), pgrx::spi::Error> {
        let result = Spi::explain("SELECT 1")?;
        assert!(result.0.get(0).unwrap().get("Plan").is_some());
        Ok(())
    }

    #[pg_test]
    fn test_spi_explain_with_args() -> Result<(), pgrx::spi::Error> {
        let i = 1 as i32;
        let j = 2 as i64;

        let result = Spi::explain_with_args("SELECT $1 + $2 = 3", &[i.into(), j.into()])?;

        assert!(result.0.get(0).unwrap().get("Plan").is_some());
        Ok(())
    }

    #[pg_extern]
    fn do_panic() {
        panic!("did a panic");
    }

    #[pg_test(error = "did a panic")]
    fn test_panic_via_spi() {
        Spi::run("SELECT tests.do_panic();").expect("SPI failed");
    }

    #[pg_test]
    fn test_inserting_null() -> Result<(), pgrx::spi::Error> {
        Spi::connect_mut(|client| {
            client.update("CREATE TABLE tests.null_test (id uuid)", None, &[]).map(|_| ())
        })?;
        assert_eq!(
            Spi::get_one_with_args::<i32>(
                "INSERT INTO tests.null_test VALUES ($1) RETURNING 1",
                &[DatumWithOid::null::<Uuid>()],
            )?
            .unwrap(),
            1
        );
        Ok(())
    }

    fn sum_all(table: pgrx::spi::SpiTupleTable) -> i32 {
        table
            .map(|r| r.get_datum_by_ordinal(1)?.value::<i32>())
            .map(|r| r.expect("failed to get ordinal #1").expect("ordinal #1 was null"))
            .sum()
    }

    #[pg_test]
    fn test_cursor() -> Result<(), spi::Error> {
        Spi::connect_mut(|client| {
            client.update("CREATE TABLE tests.cursor_table (id int)", None, &[])?;
            client.update(
                "INSERT INTO tests.cursor_table (id) \
            SELECT i FROM generate_series(1, 10) AS t(i)",
                None,
                &[],
            )?;
            let mut portal = client.open_cursor("SELECT * FROM tests.cursor_table", &[]);

            assert_eq!(sum_all(portal.fetch(3)?), 1 + 2 + 3);
            assert_eq!(sum_all(portal.fetch(3)?), 4 + 5 + 6);
            assert_eq!(sum_all(portal.fetch(3)?), 7 + 8 + 9);
            assert_eq!(sum_all(portal.fetch(3)?), 10);
            Ok(())
        })
    }

    #[pg_test]
    fn test_cursor_prepared_statement() -> Result<(), pgrx::spi::Error> {
        Spi::connect_mut(|client| {
            client.update("CREATE TABLE tests.cursor_table (id int)", None, &[])?;
            client.update(
                "INSERT INTO tests.cursor_table (id) \
            SELECT i FROM generate_series(1, 10) AS t(i)",
                None,
                &[],
            )?;
            let prepared = client.prepare("SELECT * FROM tests.cursor_table", &[])?;
            let mut portal = client.open_cursor(&prepared, &[]);

            assert_eq!(sum_all(portal.fetch(3)?), 1 + 2 + 3);
            assert_eq!(sum_all(portal.fetch(3)?), 4 + 5 + 6);
            assert_eq!(sum_all(portal.fetch(3)?), 7 + 8 + 9);
            assert_eq!(sum_all(portal.fetch(3)?), 10);
            Ok(())
        })
    }

    #[pg_test]
    #[should_panic(expected = "PreparedStatementArgumentMismatch { expected: 1, got: 0 }")]
    fn test_cursor_prepared_statement_panics_less_args() -> Result<(), pgrx::spi::Error> {
        test_cursor_prepared_statement_panics_impl(&[])
    }

    #[pg_test]
    #[should_panic(expected = "PreparedStatementArgumentMismatch { expected: 1, got: 2 }")]
    fn test_cursor_prepared_statement_panics_more_args() -> Result<(), pgrx::spi::Error> {
        test_cursor_prepared_statement_panics_impl(&[
            DatumWithOid::null::<i32>(),
            DatumWithOid::null::<i32>(),
        ])
    }

    fn test_cursor_prepared_statement_panics_impl(
        args: &[DatumWithOid],
    ) -> Result<(), pgrx::spi::Error> {
        Spi::connect_mut(|client| {
            client.update("CREATE TABLE tests.cursor_table (id int)", None, &[])?;
            client.update(
                "INSERT INTO tests.cursor_table (id) \
            SELECT i FROM generate_series(1, 10) AS t(i)",
                None,
                &[],
            )?;
            let prepared =
                client.prepare("SELECT * FROM tests.cursor_table WHERE id = $1", &oids_of![i32])?;
            client.open_cursor(&prepared, args);
            unreachable!();
        })
    }

    #[pg_test]
    fn test_cursor_by_name() -> Result<(), pgrx::spi::Error> {
        let cursor_name = Spi::connect_mut(|client| {
            client.update("CREATE TABLE tests.cursor_table (id int)", None, &[])?;
            client.update(
                "INSERT INTO tests.cursor_table (id) \
            SELECT i FROM generate_series(1, 10) AS t(i)",
                None,
                &[],
            )?;
            let mut cursor = client.open_cursor("SELECT * FROM tests.cursor_table", &[]);
            assert_eq!(sum_all(cursor.fetch(3)?), 1 + 2 + 3);
            Ok::<_, spi::Error>(cursor.detach_into_name())
        })?;

        Spi::connect(|client| {
            let mut cursor = client.find_cursor(&cursor_name)?;
            assert_eq!(sum_all(cursor.fetch(3)?), 4 + 5 + 6);
            assert_eq!(sum_all(cursor.fetch(3)?), 7 + 8 + 9);
            cursor.detach_into_name();
            Ok::<_, spi::Error>(())
        })?;

        Spi::connect(|client| {
            let mut cursor = client.find_cursor(&cursor_name)?;
            assert_eq!(sum_all(cursor.fetch(3)?), 10);
            Ok::<_, spi::Error>(())
        })?;
        Ok(())
    }

    #[pg_test(error = "syntax error at or near \"THIS\"")]
    fn test_cursor_failure() {
        Spi::connect(|client| {
            client.open_cursor("THIS IS NOT SQL", &[]);
        })
    }

    #[pg_test(error = "cursor: CursorNotFound(\"NOT A CURSOR\")")]
    fn test_cursor_not_found() {
        Spi::connect(|client| client.find_cursor("NOT A CURSOR").map(|_| ())).expect("cursor");
    }

    #[pg_test]
    fn test_columns() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let res = client.select("SELECT 42 AS a, 'test' AS b", None, &[])?;

            assert_eq!(Ok(2), res.columns());
            assert_eq!(res.column_type_oid(1).unwrap(), PgOid::BuiltIn(PgBuiltInOids::INT4OID));
            assert_eq!(res.column_type_oid(2).unwrap(), PgOid::BuiltIn(PgBuiltInOids::TEXTOID));
            assert_eq!(res.column_name(1).unwrap(), "a");
            assert_eq!(res.column_name(2).unwrap(), "b");
            Ok::<_, spi::Error>(())
        })?;

        Spi::connect_mut(|client| {
            let res = client.update("SET TIME ZONE 'PST8PDT'", None, &[])?;

            assert_eq!(Err(spi::Error::NoTupleTable), res.columns());
            Ok(())
        })
    }

    #[pg_test]
    fn test_connect_return_anything() {
        struct T;
        assert!(matches!(Spi::connect(|_| Ok::<_, spi::Error>(Some(T))).unwrap().unwrap(), T));
    }

    #[pg_test]
    fn test_spi_non_mut() -> Result<(), pgrx::spi::Error> {
        // Ensures cursor APIs do not need mutable reference to SpiClient
        Spi::connect(|client| {
            let cursor = client.open_cursor("SELECT 1", &[]).detach_into_name();
            client.find_cursor(&cursor).map(|_| ())
        })
    }

    #[pg_test]
    fn test_open_multiple_tuptables() -> Result<(), spi::Error> {
        // Regression test to ensure a new `SpiTupTable` instance does not override the
        // effective length of an already open one due to misuse of Spi statics
        Spi::connect(|client| {
            let a = client.select("SELECT 1", None, &[])?.first();
            let _b = client.select("SELECT 1 WHERE 'f'", None, &[])?;
            assert!(!a.is_empty());
            assert_eq!(1, a.len());
            assert!(a.get_heap_tuple().is_ok());
            assert_eq!(Ok(Some(1)), a.get::<i32>(1));
            Ok(())
        })
    }

    #[pg_test]
    fn test_open_multiple_tuptables_rev() -> Result<(), spi::Error> {
        // Regression test to ensure a new `SpiTupTable` instance does not override the
        // effective length of an already open one.
        // Same as `test_open_multiple_tuptables`, but with the second tuptable being empty
        Spi::connect(|client| {
            let a = client.select("SELECT 1 WHERE 'f'", None, &[])?.first();
            let _b = client.select("SELECT 1", None, &[])?;
            assert!(a.is_empty());
            assert_eq!(0, a.len());
            assert!(a.get_heap_tuple().is_ok());
            assert_eq!(Err(pgrx::spi::Error::InvalidPosition), a.get::<i32>(1));
            Ok(())
        })
    }

    #[pg_test]
    fn test_prepared_statement() -> Result<(), spi::Error> {
        let rc = Spi::connect(|client| {
            let prepared = client.prepare("SELECT $1", &oids_of![i32])?;
            client.select(&prepared, None, &[42.into()])?.first().get::<i32>(1)
        })?;

        assert_eq!(42, rc.expect("SPI failed to return proper value"));
        Ok(())
    }

    #[pg_test]
    fn test_prepared_statement_argument_mismatch() {
        let err = Spi::connect(|client| {
            let prepared = client.prepare("SELECT $1", &oids_of![i32])?;
            client.select(&prepared, None, &[]).map(|_| ())
        })
        .unwrap_err();

        assert!(matches!(
            err,
            spi::Error::PreparedStatementArgumentMismatch { expected: 1, got: 0 }
        ));
    }

    #[pg_test]
    fn test_owned_prepared_statement() -> Result<(), spi::Error> {
        let prepared = Spi::connect(|client| {
            Ok::<_, spi::Error>(client.prepare("SELECT $1", &oids_of![i32])?.keep())
        })?;
        let rc = Spi::connect(|client| {
            client.select(&prepared, None, &[42.into()])?.first().get::<i32>(1)
        })?;

        assert_eq!(Some(42), rc);
        Ok(())
    }

    #[pg_test]
    fn test_option() {
        assert!(Spi::get_one::<i32>("SELECT NULL::integer").unwrap().is_none());
    }

    #[pg_test(error = "CREATE TABLE is not allowed in a non-volatile function")]
    fn test_readwrite_in_readonly() -> Result<(), spi::Error> {
        // This is supposed to run in read-only
        Spi::connect(|client| client.select("CREATE TABLE a ()", None, &[]).map(|_| ()))
    }

    #[pg_test]
    fn test_readwrite_in_select_readwrite() -> Result<(), spi::Error> {
        Spi::connect_mut(|client| {
            // This is supposed to switch connection to read-write and run it there
            client.update("CREATE TABLE a (id INT)", None, &[])?;
            // This is supposed to run in read-write
            client.select("INSERT INTO a VALUES (1)", None, &[])?;
            Ok(())
        })
    }

    #[pg_test(error = "CREATE TABLE is not allowed in a non-volatile function")]
    fn test_execute_prepared_statement_in_readonly() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let stmt = client.prepare("CREATE TABLE a ()", &[])?;
            // This is supposed to run in read-only
            stmt.execute(&client, Some(1), &[])?;
            Ok(())
        })
    }

    #[pg_test]
    fn test_execute_prepared_statement_in_readwrite() -> Result<(), spi::Error> {
        Spi::connect(|client| {
            let stmt = client.prepare_mut("CREATE TABLE a ()", &[])?;
            // This is supposed to run in read-write
            stmt.execute(&client, Some(1), &[])?;
            Ok(())
        })
    }

    #[pg_test]
    fn test_spi_select_sees_update() -> spi::Result<()> {
        let with_select = Spi::connect_mut(|client| {
            client.update("CREATE TABLE asd(id int)", None, &[])?;
            client.update("INSERT INTO asd(id) VALUES (1)", None, &[])?;
            client.select("SELECT COUNT(*) FROM asd", None, &[])?.first().get_one::<i64>()
        })?;
        let with_get_one = Spi::get_one::<i64>("SELECT COUNT(*) FROM asd")?;

        assert_eq!(with_select, with_get_one);
        Ok(())
    }

    #[pg_test]
    fn test_spi_select_sees_run() -> spi::Result<()> {
        Spi::run("CREATE TABLE asd(id int)")?;
        Spi::run("INSERT INTO asd(id) VALUES (1)")?;
        let with_select = Spi::connect(|client| {
            client.select("SELECT COUNT(*) FROM asd", None, &[])?.first().get_one::<i64>()
        })?;
        let with_get_one = Spi::get_one::<i64>("SELECT COUNT(*) FROM asd")?;

        assert_eq!(with_select, with_get_one);
        Ok(())
    }

    #[pg_test]
    fn test_spi_select_sees_update_in_other_session() -> spi::Result<()> {
        Spi::connect_mut::<spi::Result<()>, _>(|client| {
            client.update("CREATE TABLE asd(id int)", None, &[])?;
            client.update("INSERT INTO asd(id) VALUES (1)", None, &[])?;
            Ok(())
        })?;
        let with_select = Spi::connect(|client| {
            client.select("SELECT COUNT(*) FROM asd", None, &[])?.first().get_one::<i64>()
        })?;
        let with_get_one = Spi::get_one::<i64>("SELECT COUNT(*) FROM asd")?;

        assert_eq!(with_select, with_get_one);
        Ok(())
    }

    #[pg_test]
    fn spi_can_read_domain_types() -> spi::Result<Option<String>> {
        Spi::run("CREATE DOMAIN my_text_type TEXT")?;
        Spi::get_one::<String>("SELECT 'hello'::my_text_type")
    }

    #[pg_test]
    fn spi_can_read_domain_types_based_on_domain_types() -> spi::Result<Option<String>> {
        Spi::run("CREATE DOMAIN my_text_type TEXT")?;
        Spi::run("CREATE DOMAIN my_other_text_type my_text_type")?;
        Spi::get_one::<String>("SELECT 'hello'::my_other_text_type")
    }

    #[pg_test]
    fn spi_can_read_binary_coercible_types() -> spi::Result<Option<pgrx::Inet>> {
        // cidr is binary coercible to inet
        Spi::get_one::<pgrx::Inet>("select '10.0.0.1/32'::cidr")
    }

    #[pg_test]
    fn test_quote_identifier() {
        assert_eq!("unquoted", spi::quote_identifier("unquoted"));
        assert_eq!(r#""actually-quoted""#, spi::quote_identifier("actually-quoted"));
        assert_eq!(r#""quoted-string""#, spi::quote_identifier(String::from("quoted-string")));
    }

    #[pg_test]
    fn test_quote_qualified_identifier() {
        assert_eq!(
            r#"unquoted."actually-quoted""#,
            spi::quote_qualified_identifier("unquoted", "actually-quoted")
        );
        assert_eq!(
            r#""actually-quoted".unquoted"#,
            spi::quote_qualified_identifier("actually-quoted", "unquoted")
        );
        assert_eq!(
            r#""actually-quoted1"."actually-quoted2""#,
            spi::quote_qualified_identifier("actually-quoted1", "actually-quoted2")
        );
    }

    #[pg_test]
    fn test_quote_literal() {
        assert_eq!("'quoted'", spi::quote_literal("quoted"));
        assert_eq!("'quoted-with-''quotes'''", spi::quote_literal("quoted-with-'quotes'"));
        assert_eq!("'quoted-string'", spi::quote_literal(String::from("quoted-string")));
    }

    #[pg_test]
    fn can_return_borrowed_str() -> Result<(), Box<dyn Error>> {
        let res = Spi::connect(|c| {
            let mut cursor = c.open_cursor("SELECT 'hello' FROM generate_series(1, 10000)", &[]);
            let table = cursor.fetch(10000)?;
            table.into_iter().map(|row| row.get::<&str>(1)).collect::<Result<Vec<_>, _>>()
        })?;

        let value = res.first().cloned().flatten().map(|s| s.to_string());
        assert_eq!(Some("hello".to_string()), value);
        Ok(())
    }
}
