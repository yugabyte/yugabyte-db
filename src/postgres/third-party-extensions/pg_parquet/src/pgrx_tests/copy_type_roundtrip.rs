#[pgrx::pg_schema]
mod tests {
    use std::vec;

    use crate::pgrx_tests::common::{
        assert_double, assert_float, assert_int_text_map, assert_json, assert_jsonb,
        extension_exists, timetz_array_to_utc_time_array, timetz_to_utc_time, TestResult,
        TestTable, LOCAL_TEST_FILE_PATH,
    };
    use crate::type_compat::fallback_to_text::FallbackToText;
    use crate::type_compat::geometry::{
        Geometry, GeometryColumnsMetadata, GeometryEncoding, GeometryType,
    };
    use crate::type_compat::map::Map;
    use crate::type_compat::pg_arrow_type_conversions::{
        DEFAULT_UNBOUNDED_NUMERIC_PRECISION, DEFAULT_UNBOUNDED_NUMERIC_SCALE,
    };
    use pgrx::pg_sys::Oid;
    use pgrx::{
        composite_type,
        datum::{Date, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone},
        AnyNumeric, Spi,
    };
    use pgrx::{pg_test, Json, JsonB, Uuid};

    #[pg_test]
    fn test_int2() {
        let test_table = TestTable::<i16>::new("int2".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_int2_array() {
        let test_table = TestTable::<Vec<Option<i16>>>::new("int2[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int2[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_int4() {
        let test_table = TestTable::<i32>::new("int4".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_int4_array() {
        let test_table: TestTable<Vec<Option<i32>>> =
            TestTable::<Vec<Option<i32>>>::new("int4[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int4[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_int8() {
        let test_table = TestTable::<i64>::new("int8".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_int8_array() {
        let test_table = TestTable::<Vec<Option<i64>>>::new("int8[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int8[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_float4() {
        let test_table = TestTable::<f32>::new("float4".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1.0), (2.23213123), (null), ('nan'), ('infinity'), ('-infinity');");

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        assert_float(expected, result);
    }

    #[pg_test]
    fn test_float4_array() {
        let test_table = TestTable::<Vec<Option<f32>>>::new("float4[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1.123,2.2,null,'nan','infinity','-infinity']), (null), (array[1]), (array[]::float4[]);",
        );

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                assert_float(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_float8() {
        let test_table = TestTable::<f64>::new("float8".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1.0), (2.23213123), (null), ('nan'), ('infinity'), ('-infinity');");

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        assert_double(expected, result);
    }

    #[pg_test]
    fn test_float8_array() {
        let test_table = TestTable::<Vec<Option<f64>>>::new("float8[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1.123,2.2,null,'nan','infinity','-infinity']), (null), (array[1]), (array[]::float8[]);",
        );

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                assert_double(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_bool() {
        let test_table = TestTable::<bool>::new("bool".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (false), (true), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bool_array() {
        let test_table = TestTable::<Vec<Option<bool>>>::new("bool[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array[false,true,false]), (array[true,false,null]), (null), (array[]::bool[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_text() {
        let test_table = TestTable::<String>::new("text".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_text_array() {
        let test_table = TestTable::<Vec<Option<String>>>::new("text[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::text[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_varchar() {
        let test_table = TestTable::<FallbackToText>::new("varchar".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_varchar_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("varchar[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::varchar[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bpchar() {
        let test_table = TestTable::<FallbackToText>::new("bpchar".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bpchar_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("bpchar[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::bpchar[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_name() {
        let test_table = TestTable::<FallbackToText>::new("name".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_name_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("name[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::name[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_enum() {
        let create_enum_query = "CREATE TYPE color AS ENUM ('red', 'green', 'blue');";
        Spi::run(create_enum_query).unwrap();

        let test_table = TestTable::<FallbackToText>::new("color".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('red'), ('blue'), ('green'), (null);");
        test_table.assert_expected_and_result_rows();

        let drop_enum_query = "DROP TYPE color CASCADE;";
        Spi::run(drop_enum_query).unwrap();
    }

    #[pg_test]
    fn test_enum_array() {
        let create_enum_query = "CREATE TYPE color AS ENUM ('red', 'green', 'blue');";
        Spi::run(create_enum_query).unwrap();

        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("color[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['red','blue','green',null]::color[]), (array['blue']::color[]), (null), (array[]::color[]);");
        test_table.assert_expected_and_result_rows();

        let drop_enum_query = "DROP TYPE color CASCADE;";
        Spi::run(drop_enum_query).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "invalid input value for enum color: \"red\"")]
    fn test_enum_invalid_value() {
        let create_enum_query = "CREATE TYPE color AS ENUM ('green', 'blue');";
        Spi::run(create_enum_query).unwrap();

        let test_table = TestTable::<FallbackToText>::new("color".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('red');");
        test_table.assert_expected_and_result_rows();

        let drop_enum_query = "DROP TYPE color CASCADE;";
        Spi::run(drop_enum_query).unwrap();
    }

    #[pg_test]
    fn test_bit() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('1'), ('1'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bit_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("bit[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['1','0','1']::bit[]), (array['1']::bit[]), (null), (array[]::bit[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "\"a\" is not a valid binary digit")]
    fn test_bit_invalid_value() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('a');");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "bit string length 2 does not match type bit(1)")]
    fn test_bit_invalid_length() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('01');");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_varbit() {
        let test_table = TestTable::<FallbackToText>::new("varbit".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('0101'), ('1'), ('1111110010101'), (null);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_varbit_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("varbit[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['0101','1','1111110010101',null]::varbit[]), (null), (array[]::varbit[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_char() {
        let test_table = TestTable::<i8>::new("\"char\"".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('a'), ('b'), ('c'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_char_array() {
        let test_table = TestTable::<Vec<Option<i8>>>::new("\"char\"[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array['a','b','c',null]), (null), (array[]::\"char\"[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bytea() {
        let test_table = TestTable::<Vec<u8>>::new("bytea".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (E'\\\\x010203'), (E'\\\\x040506'), (null);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_bytea_array() {
        let test_table = TestTable::<pgrx::Array<&[u8]>>::new("bytea[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[E'\\\\x010203',E'\\\\x040506',null]::bytea[]), (null), (array[]::bytea[]);",
        );
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(actual.is_none());
            }

            if expected.is_some() {
                assert!(actual.is_some());

                let expected = expected.unwrap();
                let actual = actual.unwrap();

                for (expected, actual) in expected.iter().zip(actual.iter()) {
                    assert_eq!(expected, actual);
                }
            }
        }
    }

    #[pg_test]
    fn test_oid() {
        let test_table = TestTable::<Oid>::new("oid".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_oid_array() {
        let test_table = TestTable::<Vec<Option<Oid>>>::new("oid[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[]::oid[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_map() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            return;
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        Spi::run("SELECT crunchy_map.create('int','text');").unwrap();

        let test_table = TestTable::<Map>::new("crunchy_map.key_int_val_text".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('{\"(1,)\",\"(2,myself)\",\"(3,ddd)\"}'::crunchy_map.key_int_val_text), (NULL);");

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            assert_int_text_map(expected, actual);
        }
    }

    #[pg_test]
    fn test_map_array() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            return;
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        Spi::run("SELECT crunchy_map.create('int','text');").unwrap();

        let test_table =
            TestTable::<Vec<Option<Map>>>::new("crunchy_map.key_int_val_text[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['{\"(1,)\",\"(2,myself)\",\"(3,ddd)\"}']::crunchy_map.key_int_val_text[]), (NULL), (array[]::crunchy_map.key_int_val_text[]);");

        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(actual.is_none());
            } else {
                assert!(actual.is_some());

                let expected = expected.unwrap();
                let actual = actual.unwrap();

                for (expected, actual) in expected.into_iter().zip(actual.into_iter()) {
                    assert_int_text_map(expected, actual);
                }
            }
        }
    }

    #[pg_test]
    fn test_table_with_multiple_maps() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            return;
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        Spi::run("SELECT crunchy_map.create('int','text');").unwrap();
        Spi::run("SELECT crunchy_map.create('varchar','int');").unwrap();

        let create_expected_table = "CREATE TABLE test_expected (a crunchy_map.key_int_val_text, b crunchy_map.key_varchar_val_int);";
        Spi::run(create_expected_table).unwrap();

        let insert = "INSERT INTO test_expected (a, b) VALUES ('{\"(1,)\",\"(2,myself)\",\"(3,ddd)\"}'::crunchy_map.key_int_val_text, '{\"(a,1)\",\"(b,2)\",\"(c,3)\"}'::crunchy_map.key_varchar_val_int);";
        Spi::run(insert).unwrap();

        let copy_to = format!("COPY test_expected TO '{LOCAL_TEST_FILE_PATH}'");
        Spi::run(&copy_to).unwrap();

        let create_result_table = "CREATE TABLE test_result (a crunchy_map.key_int_val_text, b crunchy_map.key_varchar_val_int);";
        Spi::run(create_result_table).unwrap();

        let copy_from = format!("COPY test_result FROM '{LOCAL_TEST_FILE_PATH}'");
        Spi::run(&copy_from).unwrap();

        let expected_a = Spi::connect(|client| {
            let query = "SELECT (crunchy_map.entries(a)).* from test_expected;";
            let tup_table = client.select(query, None, &[]).unwrap();

            let mut results = Vec::new();

            for row in tup_table {
                let key = row["key"].value::<i32>().unwrap().unwrap();
                let val = row["value"].value::<String>().unwrap();
                results.push((key, val));
            }

            results
        });

        assert_eq!(
            expected_a,
            vec![
                (1, None),
                (2, Some("myself".into())),
                (3, Some("ddd".into()))
            ]
        );

        let expected_b = Spi::connect(|client| {
            let query = "SELECT (crunchy_map.entries(b)).* from test_expected;";
            let tup_table = client.select(query, None, &[]).unwrap();

            let mut results = Vec::new();

            for row in tup_table {
                let key = row["key"].value::<String>().unwrap().unwrap();
                let val = row["value"].value::<i32>().unwrap().unwrap();
                results.push((key, val));
            }

            results
        });

        assert_eq!(
            expected_b,
            vec![("a".into(), 1), ("b".into(), 2), ("c".into(), 3)]
        );
    }

    #[pg_test]
    #[should_panic(expected = "MapArray entries cannot contain nulls")]
    fn test_map_null_entries() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            // let the test pass
            panic!("MapArray entries cannot contain nulls");
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        Spi::run("SELECT crunchy_map.create('int','text');").unwrap();

        let create_table = "CREATE TABLE test_table (a crunchy_map.key_int_val_text);";
        Spi::run(create_table).unwrap();

        let insert =
            "INSERT INTO test_table (a) VALUES (array[null]::crunchy_map.key_int_val_text);";
        Spi::run(insert).unwrap();

        let copy_to = format!(
            "COPY (SELECT a FROM test_table) TO '{}'",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&copy_to).unwrap();
    }

    #[pg_test]
    #[should_panic(
        expected = "Found unmasked nulls for non-nullable StructArray field \\\"key\\\""
    )]
    fn test_map_null_entry_key() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            // let the test pass
            panic!("Found unmasked nulls for non-nullable StructArray field \\\"key\\\"");
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        Spi::run("SELECT crunchy_map.create('int','text');").unwrap();

        let create_table = "CREATE TABLE test_table (a crunchy_map.key_int_val_text);";
        Spi::run(create_table).unwrap();

        let insert =
            "INSERT INTO test_table (a) VALUES ('{\"(,tt)\"}'::crunchy_map.key_int_val_text);";
        Spi::run(insert).unwrap();

        let copy_to = format!(
            "COPY (SELECT a FROM test_table) TO '{}'",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&copy_to).unwrap();
    }

    #[pg_test]
    fn test_date() {
        let test_table = TestTable::<Date>::new("date".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('2022-05-01'), ('2022-05-02'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_date_array() {
        let test_table = TestTable::<Vec<Option<Date>>>::new("date[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['2022-05-01','2022-05-02',null]::date[]), (null), (array[]::date[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_time() {
        let test_table = TestTable::<Time>::new("time".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('15:00:00'), ('15:30:12'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_time_array() {
        let test_table = TestTable::<Vec<Option<Time>>>::new("time[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['15:00:00','15:30:12',null]::time[]), (null), (array[]::time[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_timetz() {
        let test_table = TestTable::<TimeWithTimeZone>::new("timetz".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('15:00:00+03'), ('15:30:12-03'), (null);",
        );
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        // timetz is converted to utc timetz after copying to parquet,
        // so we need to the results to utc before comparing them
        let expected = expected
            .into_iter()
            .map(|(timetz,)| (timetz.and_then(timetz_to_utc_time),))
            .collect::<Vec<_>>();

        let result = result
            .into_iter()
            .map(|(timetz,)| (timetz.and_then(timetz_to_utc_time),))
            .collect::<Vec<_>>();

        let normalized_test_result = TestResult { expected, result };

        normalized_test_result.assert();
    }

    #[pg_test]
    fn test_timetz_array() {
        let test_table = TestTable::<Vec<Option<TimeWithTimeZone>>>::new("timetz[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['15:00:00+03','15:30:12-03',null]::timetz[]), (null), (array[]::timetz[]);",
        );
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        // timetz is converted to utc timetz after copying to parquet,
        // so we need to the results to utc before comparing them
        let expected = expected
            .into_iter()
            .map(|(timetz,)| (timetz.and_then(timetz_array_to_utc_time_array),))
            .collect::<Vec<_>>();

        let result = result
            .into_iter()
            .map(|(timetz,)| (timetz.and_then(timetz_array_to_utc_time_array),))
            .collect::<Vec<_>>();

        let normalized_test_result = TestResult { expected, result };

        normalized_test_result.assert();
    }

    #[pg_test]
    fn test_timestamp() {
        let test_table = TestTable::<Timestamp>::new("timestamp".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('2022-05-01 15:00:00'), ('2022-05-02 15:30:12'), (null);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_timestamp_array() {
        let test_table = TestTable::<Vec<Option<Timestamp>>>::new("timestamp[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['2022-05-01 15:00:00','2022-05-02 15:30:12',null]::timestamp[]), (null), (array[]::timestamp[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_timestamptz() {
        let test_table = TestTable::<TimestampWithTimeZone>::new("timestamptz".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('2022-05-01 15:00:00+03'), ('2022-05-02 15:30:12-03'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_timestamptz_array() {
        let test_table =
            TestTable::<Vec<Option<TimestampWithTimeZone>>>::new("timestamptz[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['2022-05-01 15:00:00+03','2022-05-02 15:30:12-03',null]::timestamptz[]), (null), (array[]::timestamptz[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_interval() {
        let test_table = TestTable::<FallbackToText>::new("interval".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('15 years 10 months 1 day 10:00:00'), ('5 days 4 minutes 10 seconds'), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_interval_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("interval[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['15 years 10 months 1 day 10:00:00','5 days 4 minutes 10 seconds',null]::interval[]), (null), (array[]::interval[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_uuid() {
        let test_table = TestTable::<Uuid>::new("uuid".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (gen_random_uuid()), (gen_random_uuid()), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_uuid_array() {
        let test_table = TestTable::<Vec<Option<Uuid>>>::new("uuid[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[gen_random_uuid(),gen_random_uuid(),null]), (null), (array[]::uuid[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_json() {
        let test_table = TestTable::<Json>::new("json".into()).with_order_by_col("a->>'a'".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('{\"a\":\"test_json_1\"}'), ('{\"a\":\"test_json_2\"}'), (null);");
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        assert_json(expected, result);
    }

    #[pg_test]
    fn test_json_array() {
        let test_table = TestTable::<Vec<Option<Json>>>::new("json[]".into())
            .with_order_by_col("a::text[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['{\"a\":\"test_json_1\"}','{\"a\":\"test_json_2\"}',null]::json[]), (null), (array[]::json[]);");
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                assert_json(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_jsonb() {
        let test_table =
            TestTable::<JsonB>::new("jsonb".into()).with_order_by_col("a->>'a'".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('{\"a\":\"test_jsonb_1\"}'), ('{\"a\":\"test_jsonb_2\"}'), (null);");
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        assert_jsonb(expected, result);
    }

    #[pg_test]
    fn test_jsonb_array() {
        let test_table = TestTable::<Vec<Option<JsonB>>>::new("jsonb[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['{\"a\":\"test_jsonb_1\"}','{\"a\":\"test_jsonb_2\"}',null]::jsonb[]), (null), (array[]::jsonb[]);");
        let TestResult { expected, result } = test_table.select_expected_and_result_rows();

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                assert_jsonb(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_small_numeric() {
        let attribute_schema_getter = || -> Vec<(Option<i32>, Option<i32>, String, String)> {
            Spi::connect(|client| {
                let parquet_schema_command = format!("select precision, scale, logical_type, type_name from parquet.schema('{}') WHERE name = 'a';", LOCAL_TEST_FILE_PATH);

                let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
                let mut results = Vec::new();

                for row in tup_table {
                    let precision = row["precision"].value::<i32>().unwrap();
                    let scale = row["scale"].value::<i32>().unwrap();
                    let logical_type = row["logical_type"].value::<String>().unwrap().unwrap();
                    let physical_type = row["type_name"].value::<String>().unwrap().unwrap();

                    results.push((precision, scale, logical_type, physical_type));
                }

                results
            })
        };

        // (P <= 9) => INT32
        let test_table = TestTable::<AnyNumeric>::new("numeric(9,4)".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.020), (-2.12313), (.3), (4), (null);");
        test_table.assert_expected_and_result_rows();

        let attribute_schema = attribute_schema_getter();
        assert_eq!(attribute_schema.len(), 1);
        assert_eq!(
            attribute_schema[0],
            (Some(9), Some(4), "DECIMAL".to_string(), "INT32".to_string())
        );

        // (9 < P <= 18) => INT64
        let test_table = TestTable::<AnyNumeric>::new("numeric(18,4)".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.020), (-2.12313), (.3), (4), (null);");
        test_table.assert_expected_and_result_rows();

        let attribute_schema = attribute_schema_getter();
        assert_eq!(attribute_schema.len(), 1);
        assert_eq!(
            attribute_schema[0],
            (
                Some(18),
                Some(4),
                "DECIMAL".to_string(),
                "INT64".to_string()
            )
        );

        // (18 < P <= 38) => FIXED_LEN_BYTE_ARRAY(9-16)
        let test_table = TestTable::<AnyNumeric>::new("numeric(38,4)".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.020), (-2.12313), (.3), (4), (null);");
        test_table.assert_expected_and_result_rows();

        let attribute_schema = attribute_schema_getter();
        assert_eq!(attribute_schema.len(), 1);
        assert_eq!(
            attribute_schema[0],
            (
                Some(38),
                Some(4),
                "DECIMAL".to_string(),
                "FIXED_LEN_BYTE_ARRAY".to_string()
            )
        );
    }

    #[pg_test]
    fn test_small_numeric_array() {
        let test_table = TestTable::<Vec<Option<AnyNumeric>>>::new("numeric(10,4)[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[0.0,.0,1.,+1.020,-2.12313,.3,4,null]), (null), (array[]::numeric(10,4)[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_large_numeric() {
        let large_precision = DEFAULT_UNBOUNDED_NUMERIC_PRECISION + 1;

        let test_table =
            TestTable::<FallbackToText>::new(format!("numeric({},4)", large_precision));
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.020), (2.12313), (3), (null);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_large_numeric_array() {
        let large_precision = DEFAULT_UNBOUNDED_NUMERIC_PRECISION + 1;

        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new(format!(
            "numeric({},4)[]",
            large_precision
        ));
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[0.0,.0,1.,1.020,2.12313,3,null]), (null), (array[]::numeric(100,4)[]);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_unbounded_numeric() {
        let test_table = TestTable::<AnyNumeric>::new("numeric".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.02), (2.12), (3), (null);",
        );
        test_table.assert_expected_and_result_rows();

        let parquet_schema_command =
            format!("select precision, scale, logical_type, type_name from parquet.schema('{}') WHERE name = 'a';", LOCAL_TEST_FILE_PATH);

        let attribute_schema = Spi::connect(|client| {
            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let precision = row["precision"].value::<i32>().unwrap();
                let scale = row["scale"].value::<i32>().unwrap();
                let logical_type = row["logical_type"].value::<String>().unwrap().unwrap();
                let physical_type = row["type_name"].value::<String>().unwrap().unwrap();

                results.push((precision, scale, logical_type, physical_type));
            }

            results
        });

        assert_eq!(attribute_schema.len(), 1);
        assert_eq!(
            attribute_schema[0],
            (
                Some(DEFAULT_UNBOUNDED_NUMERIC_PRECISION as _),
                Some(DEFAULT_UNBOUNDED_NUMERIC_SCALE as _),
                "DECIMAL".to_string(),
                "FIXED_LEN_BYTE_ARRAY".to_string()
            )
        );
    }

    #[pg_test]
    fn test_unbounded_numeric_array() {
        let test_table = TestTable::<Vec<Option<AnyNumeric>>>::new("numeric[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[0.0,.0,1.,1.02,2.12,3,null]), (null), (array[]::numeric[]);",
        );
        test_table.assert_expected_and_result_rows();

        let parquet_schema_command =
            format!("select precision, scale, logical_type, type_name from parquet.schema('{}') WHERE name = 'element' ORDER BY logical_type;", LOCAL_TEST_FILE_PATH);

        let attribute_schema = Spi::connect(|client| {
            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let precision = row["precision"].value::<i32>().unwrap();
                let scale = row["scale"].value::<i32>().unwrap();
                let logical_type = row["logical_type"].value::<String>().unwrap().unwrap();
                let physical_type = row["type_name"].value::<String>().unwrap();

                results.push((precision, scale, logical_type, physical_type));
            }

            results
        });

        assert_eq!(attribute_schema.len(), 1);
        assert_eq!(
            attribute_schema[0],
            (
                Some(DEFAULT_UNBOUNDED_NUMERIC_PRECISION as _),
                Some(DEFAULT_UNBOUNDED_NUMERIC_SCALE as _),
                "DECIMAL".to_string(),
                Some("FIXED_LEN_BYTE_ARRAY".to_string())
            )
        );
    }

    #[pg_test]
    #[should_panic(
        expected = "numeric value contains 30 digits before decimal point, which exceeds max allowed integral digits 29 during copy to parquet"
    )]
    fn test_invalid_unbounded_numeric_integral_digits() {
        let invalid_integral_digits =
            DEFAULT_UNBOUNDED_NUMERIC_PRECISION - DEFAULT_UNBOUNDED_NUMERIC_SCALE + 1;

        let copy_to_command = format!(
            "copy (select (repeat('1', {}) || '.2')::numeric as a) to '{}'",
            invalid_integral_digits, LOCAL_TEST_FILE_PATH
        );

        Spi::run(&copy_to_command).unwrap();
    }

    #[pg_test]
    #[should_panic(
        expected = "numeric value contains 10 digits after decimal point, which exceeds max allowed decimal digits 9 during copy to parquet"
    )]
    fn test_invalid_unbounded_numeric_decimal_digits() {
        let invalid_decimal_digits = DEFAULT_UNBOUNDED_NUMERIC_SCALE + 1;

        let copy_to_command = format!(
            "copy (select ('2.' || repeat('1', {}) )::numeric as a) to '{}'",
            invalid_decimal_digits, LOCAL_TEST_FILE_PATH
        );

        Spi::run(&copy_to_command).unwrap();
    }

    #[cfg(feature = "pg14")]
    #[pg_test]
    #[should_panic = "NUMERIC scale -2 must be between 0 and precision 5"]
    fn test_numeric_negative_scale() {
        let test_table = TestTable::<AnyNumeric>::new("numeric(5,-2)".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (1234567.1231244), (123.23223), (12.0), (-12.12303), (null), (0);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[cfg(not(feature = "pg14"))]
    #[pg_test]
    fn test_numeric_negative_scale() {
        let test_table = TestTable::<AnyNumeric>::new("numeric(5,-2)".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (1234567.1231244), (123.23223), (12.0), (-12.12303), (null), (0);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[cfg(feature = "pg14")]
    #[pg_test]
    #[should_panic = "NUMERIC scale 8 must be between 0 and precision 5"]
    fn test_numeric_with_larger_scale() {
        let test_table = TestTable::<AnyNumeric>::new("numeric(5, 8)".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.00012345), (0.00012340), (-0.00012345), (-0.00012340), (null), (0);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[cfg(not(feature = "pg14"))]
    #[pg_test]
    fn test_numeric_with_larger_scale() {
        let test_table = TestTable::<AnyNumeric>::new("numeric(5, 8)".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.00012345), (0.00012340), (-0.00012345), (-0.00012340), (null), (0);",
        );
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic = "Special numeric values like NaN, Inf, -Inf are not allowed"]
    fn test_numeric_nan() {
        let test_table = TestTable::<AnyNumeric>::new("numeric".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('NaN');");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic = "Special numeric values like NaN, Inf, -Inf are not allowed"]
    fn test_numeric_inf() {
        let test_table = TestTable::<AnyNumeric>::new("numeric".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('-Infinity');");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_geometry() {
        // Skip the test if postgis extension is not available
        if !extension_exists("postgis") {
            return;
        }

        let query = "DROP EXTENSION IF EXISTS postgis; CREATE EXTENSION postgis;";
        Spi::run(query).unwrap();

        let test_table = TestTable::<Geometry>::new("geometry".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (ST_GeomFromText('POINT(1 1)')),
                                                       (ST_GeomFromText('POLYGON((0 0, 0 1, 1 1, 1 0, 0 0))')),
                                                       (ST_GeomFromText('LINESTRING(0 0, 1 1)')),
                                                       (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_geometry_array() {
        // Skip the test if postgis extension is not available
        if !extension_exists("postgis") {
            return;
        }

        let query = "DROP EXTENSION IF EXISTS postgis; CREATE EXTENSION postgis;";
        Spi::run(query).unwrap();

        let test_table = TestTable::<Vec<Option<Geometry>>>::new("geometry[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array[ST_GeomFromText('POINT(1 1)'), ST_GeomFromText('POLYGON((0 0, 0 1, 1 1, 1 0, 0 0))'), null]), (null), (array[]::geometry[]);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_geometry_geoparquet_metadata() {
        // Skip the test if postgis extension is not available
        if !extension_exists("postgis") {
            return;
        }

        let query = "DROP EXTENSION IF EXISTS postgis; CREATE EXTENSION postgis;";
        Spi::run(query).unwrap();

        let copy_to_query = format!(
            "COPY (SELECT ST_GeomFromText('POINT(1 1)')::geometry(point) as a,
                          ST_GeomFromText('LINESTRING(0 0, 1 1)')::geometry(linestring) as b,
                          ST_GeomFromText('POLYGON((0 0, 1 1, 2 2, 0 0))')::geometry(polygon) as c,
                          ST_GeomFromText('MULTIPOINT((0 0), (1 1))')::geometry(multipoint) as d,
                          ST_GeomFromText('MULTILINESTRING((0 0, 1 1), (2 2, 3 3))')::geometry(multilinestring) as e,
                          ST_GeomFromText('MULTIPOLYGON(((0 0, 1 1, 2 2, 0 0)), ((3 3, 4 4, 5 5, 3 3)))')::geometry(multipolygon) as f,
                          ST_GeomFromText('GEOMETRYCOLLECTION(POINT(1 1), LINESTRING(0 0, 1 1))')::geometry(geometrycollection) as g
                  )
            TO '{LOCAL_TEST_FILE_PATH}' WITH (format parquet);",
        );
        Spi::run(copy_to_query.as_str()).unwrap();

        // Check geoparquet metadata
        let geoparquet_metadata_query = format!(
            "select encode(value, 'escape')::jsonb
            from parquet.kv_metadata('{LOCAL_TEST_FILE_PATH}')
            where encode(key, 'escape') = 'geo';",
        );
        let geoparquet_metadata_json = Spi::get_one::<JsonB>(geoparquet_metadata_query.as_str())
            .unwrap()
            .unwrap();

        let geoparquet_metadata: GeometryColumnsMetadata =
            serde_json::from_value(geoparquet_metadata_json.0).unwrap();

        // assert common metadata
        assert_eq!(geoparquet_metadata.version, "1.1.0");
        assert_eq!(geoparquet_metadata.primary_column, "a");

        // point
        assert_eq!(
            geoparquet_metadata.columns.get("a").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("a").unwrap().geometry_types,
            vec![GeometryType::Point]
        );

        // linestring
        assert_eq!(
            geoparquet_metadata.columns.get("b").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("b").unwrap().geometry_types,
            vec![GeometryType::LineString]
        );

        // polygon
        assert_eq!(
            geoparquet_metadata.columns.get("c").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("c").unwrap().geometry_types,
            vec![GeometryType::Polygon]
        );

        // multipoint
        assert_eq!(
            geoparquet_metadata.columns.get("d").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("d").unwrap().geometry_types,
            vec![GeometryType::MultiPoint]
        );

        // multilinestring
        assert_eq!(
            geoparquet_metadata.columns.get("e").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("e").unwrap().geometry_types,
            vec![GeometryType::MultiLineString]
        );

        // multipolygon
        assert_eq!(
            geoparquet_metadata.columns.get("f").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("f").unwrap().geometry_types,
            vec![GeometryType::MultiPolygon]
        );

        // geometrycollection
        assert_eq!(
            geoparquet_metadata.columns.get("g").unwrap().encoding,
            GeometryEncoding::WKB
        );
        assert_eq!(
            geoparquet_metadata.columns.get("g").unwrap().geometry_types,
            vec![GeometryType::GeometryCollection]
        );
    }

    #[pg_test]
    fn test_complex_composite() {
        Spi::run("CREATE TYPE dog AS (name text, age int);").unwrap();
        Spi::run("CREATE TYPE dog_owner AS (name text, dogs dog[], lucky_numbers int[]);").unwrap();
        Spi::run("CREATE TABLE dog_owners (owner dog_owner);").unwrap();

        Spi::run("INSERT INTO dog_owners VALUES (ROW('Alice', ARRAY[('Buddy', 2)::dog, ('Charlie', 3)::dog], ARRAY[1, 2, 3]));").unwrap();
        Spi::run("INSERT INTO dog_owners VALUES (ROW('Cathie', ARRAY[]::dog[], ARRAY[4, 5, 6]));")
            .unwrap();
        Spi::run("INSERT INTO dog_owners VALUES (ROW('Bob', ARRAY[('Daisy', 4)::dog, ('Ella', 5)::dog], ARRAY[4, 5, 6]));").unwrap();
        Spi::run("INSERT INTO dog_owners VALUES (ROW('Cathy', NULL, NULL));").unwrap();
        Spi::run("INSERT INTO dog_owners VALUES (NULL);").unwrap();

        let select_command = "SELECT owner FROM dog_owners ORDER BY owner;";
        let expected_result = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, &[]).unwrap();

            for row in tup_table {
                let owner = row["owner"].value::<composite_type!("dog_owner")>();
                results.push(owner.unwrap());
            }

            results
        });

        let copy_to_query = format!(
            "COPY (SELECT owner FROM dog_owners) TO '{}' WITH (format parquet);",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(copy_to_query.as_str()).unwrap();

        Spi::run("TRUNCATE dog_owners;").unwrap();

        let copy_from_query = format!(
            "COPY dog_owners FROM '{}' WITH (format parquet);",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(copy_from_query.as_str()).unwrap();

        let result = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, &[]).unwrap();

            for row in tup_table {
                let owner = row["owner"].value::<composite_type!("dog_owner")>();
                results.push(owner.unwrap());
            }

            results
        });

        for (expected, actual) in expected_result.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(actual.is_none());
            } else if expected.is_some() {
                assert!(actual.is_some());

                let expected = expected.unwrap();
                let actual = actual.unwrap();

                assert_eq!(
                    expected.get_by_name::<String>("name").unwrap(),
                    actual.get_by_name::<String>("name").unwrap()
                );

                let expected_dogs = expected
                    .get_by_name::<pgrx::Array<composite_type!("dog")>>("dogs")
                    .unwrap();
                let actual_dogs = actual
                    .get_by_name::<pgrx::Array<composite_type!("dog")>>("dogs")
                    .unwrap();

                if expected_dogs.is_none() {
                    assert!(actual_dogs.is_none());
                } else if expected_dogs.is_some() {
                    assert!(actual_dogs.is_some());

                    let expected_dogs = expected_dogs.unwrap();
                    let actual_dogs = actual_dogs.unwrap();

                    for (expected_dog, actual_dog) in expected_dogs.iter().zip(actual_dogs.iter()) {
                        if expected_dog.is_none() {
                            assert!(actual_dog.is_none());
                        } else if expected_dog.is_some() {
                            assert!(actual_dog.is_some());

                            let expected_dog = expected_dog.unwrap();
                            let actual_dog = actual_dog.unwrap();

                            assert_eq!(
                                expected_dog.get_by_name::<String>("name").unwrap(),
                                actual_dog.get_by_name::<String>("name").unwrap()
                            );

                            assert_eq!(
                                expected_dog.get_by_name::<i32>("age").unwrap(),
                                actual_dog.get_by_name::<i32>("age").unwrap()
                            );
                        }
                    }
                }

                let expected_lucky_numbers = expected
                    .get_by_name::<pgrx::Array<i32>>("lucky_numbers")
                    .unwrap();

                let actual_lucky_numbers = actual
                    .get_by_name::<pgrx::Array<i32>>("lucky_numbers")
                    .unwrap();

                if expected_lucky_numbers.is_none() {
                    assert!(actual_lucky_numbers.is_none());
                } else if expected_lucky_numbers.is_some() {
                    assert!(actual_lucky_numbers.is_some());

                    let expected_lucky_numbers = expected_lucky_numbers.unwrap();
                    let actual_lucky_numbers = actual_lucky_numbers.unwrap();

                    for (expected_lucky_number, actual_lucky_number) in expected_lucky_numbers
                        .into_iter()
                        .zip(actual_lucky_numbers.into_iter())
                    {
                        assert_eq!(expected_lucky_number, actual_lucky_number);
                    }
                }
            }
        }

        Spi::run("DROP TABLE dog_owners;").unwrap();
        Spi::run("DROP TYPE dog_owner;").unwrap();
        Spi::run("DROP TYPE dog;").unwrap();
    }
}
