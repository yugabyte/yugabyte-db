use parquet_copy_hook::hook::{init_parquet_copy_hook, ENABLE_PARQUET_COPY_HOOK};
use parquet_copy_hook::pg_compat::MarkGUCPrefixReserved;
use pgrx::{prelude::*, GucContext, GucFlags, GucRegistry};

mod arrow_parquet;
mod parquet_copy_hook;
mod parquet_udfs;
mod pgrx_utils;
mod type_compat;

// re-export external api
#[allow(unused_imports)]
pub use crate::arrow_parquet::compression::PgParquetCompression;
#[allow(unused_imports)]
pub use crate::parquet_copy_hook::copy_to_dest_receiver::create_copy_to_parquet_dest_receiver;

pgrx::pg_module_magic!();

extension_sql_file!("../sql/bootstrap.sql", name = "role_setup", bootstrap);

#[pg_guard]
pub extern "C" fn _PG_init() {
    GucRegistry::define_bool_guc(
        "pg_parquet.enable_copy_hooks",
        "Enable parquet copy hooks",
        "Enable parquet copy hooks",
        &ENABLE_PARQUET_COPY_HOOK,
        GucContext::Userset,
        GucFlags::default(),
    );

    MarkGUCPrefixReserved("pg_parquet");

    init_parquet_copy_hook();
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use std::io::Write;
    use std::marker::PhantomData;
    use std::{collections::HashMap, fmt::Debug};

    use crate::arrow_parquet::compression::PgParquetCompression;
    use crate::type_compat::fallback_to_text::FallbackToText;
    use crate::type_compat::geometry::Geometry;
    use crate::type_compat::map::Map;
    use crate::type_compat::pg_arrow_type_conversions::{
        DEFAULT_UNBOUNDED_NUMERIC_PRECISION, DEFAULT_UNBOUNDED_NUMERIC_SCALE,
    };
    use pgrx::pg_sys::Oid;
    use pgrx::{
        composite_type,
        datum::{Date, Time, TimeWithTimeZone, Timestamp, TimestampWithTimeZone},
        pg_test, AnyNumeric, FromDatum, IntoDatum, Spi,
    };
    enum CopyOptionValue {
        StringOption(String),
        IntOption(i64),
    }

    fn comma_separated_copy_options(options: &HashMap<String, CopyOptionValue>) -> String {
        let mut comma_sepated_options = String::new();

        for (option_idx, (key, value)) in options.iter().enumerate() {
            match value {
                CopyOptionValue::StringOption(value) => {
                    comma_sepated_options.push_str(&format!("{} '{}'", key, value));
                }
                CopyOptionValue::IntOption(value) => {
                    comma_sepated_options.push_str(&format!("{} {}", key, value));
                }
            }

            if option_idx < options.len() - 1 {
                comma_sepated_options.push_str(", ");
            }
        }

        comma_sepated_options
    }

    struct TestTable<T: IntoDatum + FromDatum> {
        uri: String,
        order_by_col: String,
        copy_to_options: HashMap<String, CopyOptionValue>,
        copy_from_options: HashMap<String, CopyOptionValue>,
        _data: PhantomData<T>,
    }

    impl<T: IntoDatum + FromDatum> TestTable<T> {
        fn new(typename: String) -> Self {
            Spi::run("DROP TABLE IF EXISTS test_expected, test_result;").unwrap();

            let create_table_command = format!("CREATE TABLE test_expected (a {});", &typename);
            Spi::run(create_table_command.as_str()).unwrap();

            let create_table_command = format!("CREATE TABLE test_result (a {});", &typename);
            Spi::run(create_table_command.as_str()).unwrap();

            let mut copy_to_options = HashMap::new();
            copy_to_options.insert(
                "format".to_string(),
                CopyOptionValue::StringOption("parquet".to_string()),
            );

            let mut copy_from_options = HashMap::new();
            copy_from_options.insert(
                "format".to_string(),
                CopyOptionValue::StringOption("parquet".to_string()),
            );

            let uri = "/tmp/test.parquet".to_string();

            let order_by_col = "a".to_string();

            Self {
                uri,
                order_by_col,
                copy_to_options,
                copy_from_options,
                _data: PhantomData,
            }
        }

        fn with_order_by_col(mut self, order_by_col: String) -> Self {
            self.order_by_col = order_by_col;
            self
        }

        fn with_copy_to_options(
            mut self,
            copy_to_options: HashMap<String, CopyOptionValue>,
        ) -> Self {
            self.copy_to_options = copy_to_options;
            self
        }

        fn with_copy_from_options(
            mut self,
            copy_from_options: HashMap<String, CopyOptionValue>,
        ) -> Self {
            self.copy_from_options = copy_from_options;
            self
        }

        fn with_uri(mut self, uri: String) -> Self {
            self.uri = uri;
            self
        }

        fn insert(&self, insert_command: &str) {
            Spi::run(insert_command).unwrap();
        }

        fn select_all(&self, table_name: &str) -> Vec<(Option<T>,)> {
            let select_command = format!(
                "SELECT a FROM {} ORDER BY {};",
                table_name, self.order_by_col
            );

            Spi::connect(|client| {
                let mut results = Vec::new();
                let tup_table = client.select(&select_command, None, None).unwrap();

                for row in tup_table {
                    let val = row["a"].value::<T>();
                    results.push((val.expect("could not select"),));
                }

                results
            })
        }

        fn copy_to_parquet(&self) {
            let mut copy_to_query = format!("COPY (SELECT a FROM test_expected) TO '{}'", self.uri);

            if !self.copy_to_options.is_empty() {
                copy_to_query.push_str(" WITH (");

                let options_str = comma_separated_copy_options(&self.copy_to_options);
                copy_to_query.push_str(&options_str);

                copy_to_query.push(')');
            }

            copy_to_query.push(';');

            Spi::run(copy_to_query.as_str()).unwrap();
        }

        fn copy_from_parquet(&self) {
            let mut copy_from_query = format!("COPY test_result FROM '{}'", self.uri);

            if !self.copy_from_options.is_empty() {
                copy_from_query.push_str(" WITH (");

                let options_str = comma_separated_copy_options(&self.copy_from_options);
                copy_from_query.push_str(&options_str);

                copy_from_query.push(')');
            }

            copy_from_query.push(';');

            Spi::run(copy_from_query.as_str()).unwrap();
        }
    }

    fn timetz_to_utc_time(timetz: TimeWithTimeZone) -> Option<Time> {
        Some(timetz.to_utc())
    }

    fn timetz_array_to_utc_time_array(
        timetz_array: Vec<Option<TimeWithTimeZone>>,
    ) -> Option<Vec<Option<Time>>> {
        Some(
            timetz_array
                .into_iter()
                .map(|timetz| timetz.map(|timetz| timetz.to_utc()))
                .collect(),
        )
    }

    fn assert_int_text_map(expected: Option<Map>, actual: Option<Map>) {
        if expected.is_none() {
            assert!(actual.is_none());
        } else {
            assert!(actual.is_some());

            let expected = expected.unwrap().entries;
            let actual = actual.unwrap().entries;

            for (expected, actual) in expected.iter().zip(actual.iter()) {
                if expected.is_none() {
                    assert!(actual.is_none());
                } else {
                    assert!(actual.is_some());

                    let expected = expected.unwrap();
                    let actual = actual.unwrap();

                    let expected_key: Option<i32> = expected.get_by_name("key").unwrap();
                    let actual_key: Option<i32> = actual.get_by_name("key").unwrap();

                    assert_eq!(expected_key, actual_key);

                    let expected_val: Option<String> = expected.get_by_name("val").unwrap();
                    let actual_val: Option<String> = actual.get_by_name("val").unwrap();

                    assert_eq!(expected_val, actual_val);
                }
            }
        }
    }

    struct TestResult<T> {
        expected: Vec<(Option<T>,)>,
        result: Vec<(Option<T>,)>,
    }

    fn test_common<T: IntoDatum + FromDatum>(test_table: TestTable<T>) -> TestResult<T> {
        test_table.copy_to_parquet();
        test_table.copy_from_parquet();

        let expected = test_table.select_all("test_expected");
        let result = test_table.select_all("test_result");

        TestResult { expected, result }
    }

    fn test_assert<T>(expected_result: Vec<(Option<T>,)>, result: Vec<(Option<T>,)>)
    where
        T: Debug + PartialEq,
    {
        for (expected, actual) in expected_result.into_iter().zip(result.into_iter()) {
            assert_eq!(expected, actual);
        }
    }

    fn test_assert_float(expected_result: Vec<Option<f32>>, result: Vec<Option<f32>>) {
        for (expected, actual) in expected_result.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(actual.is_none());
            }

            if expected.is_some() {
                assert!(actual.is_some());

                let expected = expected.unwrap();
                let actual = actual.unwrap();

                if expected.is_nan() {
                    assert!(actual.is_nan());
                } else if expected.is_infinite() {
                    assert!(actual.is_infinite());
                    assert!(expected.is_sign_positive() == actual.is_sign_positive());
                } else {
                    assert_eq!(expected, actual);
                }
            }
        }
    }

    fn test_assert_double(expected_result: Vec<Option<f64>>, result: Vec<Option<f64>>) {
        for (expected, actual) in expected_result.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(actual.is_none());
            }

            if expected.is_some() {
                assert!(actual.is_some());

                let expected = expected.unwrap();
                let actual = actual.unwrap();

                if expected.is_nan() {
                    assert!(actual.is_nan());
                } else if expected.is_infinite() {
                    assert!(actual.is_infinite());
                    assert!(expected.is_sign_positive() == actual.is_sign_positive());
                } else {
                    assert_eq!(expected, actual);
                }
            }
        }
    }

    fn test_helper<T: IntoDatum + FromDatum + Debug + PartialEq>(test_table: TestTable<T>) {
        let test_result = test_common(test_table);
        test_assert(test_result.expected, test_result.result);
    }

    fn extension_exists(extension_name: &str) -> bool {
        let query = format!(
            "select count(*) = 1 from pg_available_extensions where name = '{}'",
            extension_name
        );

        Spi::get_one(&query).unwrap().unwrap()
    }

    #[pg_test]
    fn test_int2() {
        let test_table = TestTable::<i16>::new("int2".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_int2_array() {
        let test_table = TestTable::<Vec<Option<i16>>>::new("int2[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int2[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_int4() {
        let test_table = TestTable::<i32>::new("int4".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_int4_array() {
        let test_table: TestTable<Vec<Option<i32>>> =
            TestTable::<Vec<Option<i32>>>::new("int4[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int4[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_int8() {
        let test_table = TestTable::<i64>::new("int8".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_int8_array() {
        let test_table = TestTable::<Vec<Option<i64>>>::new("int8[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[1]), (array[]::int8[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_float4() {
        let test_table = TestTable::<f32>::new("float4".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1.0), (2.23213123), (null), ('nan'), ('infinity'), ('-infinity');");

        let TestResult { expected, result } = test_common(test_table);

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        test_assert_float(expected, result);
    }

    #[pg_test]
    fn test_float4_array() {
        let test_table = TestTable::<Vec<Option<f32>>>::new("float4[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1.123,2.2,null,'nan','infinity','-infinity']), (null), (array[1]), (array[]::float4[]);",
        );

        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                test_assert_float(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_float8() {
        let test_table = TestTable::<f64>::new("float8".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1.0), (2.23213123), (null), ('nan'), ('infinity'), ('-infinity');");

        let TestResult { expected, result } = test_common(test_table);

        let expected = expected.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        let result = result.into_iter().map(|(val,)| val).collect::<Vec<_>>();
        test_assert_double(expected, result);
    }

    #[pg_test]
    fn test_float8_array() {
        let test_table = TestTable::<Vec<Option<f64>>>::new("float8[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1.123,2.2,null,'nan','infinity','-infinity']), (null), (array[1]), (array[]::float8[]);",
        );

        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (result,)) in expected.into_iter().zip(result.into_iter()) {
            if expected.is_none() {
                assert!(result.is_none());
            }

            if expected.is_some() {
                assert!(result.is_some());

                let expected = expected.unwrap();
                let result = result.unwrap();

                test_assert_double(expected, result);
            }
        }
    }

    #[pg_test]
    fn test_bool() {
        let test_table = TestTable::<bool>::new("bool".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (false), (true), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bool_array() {
        let test_table = TestTable::<Vec<Option<bool>>>::new("bool[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array[false,true,false]), (array[true,false,null]), (null), (array[]::bool[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_text() {
        let test_table = TestTable::<String>::new("text".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_text_array() {
        let test_table = TestTable::<Vec<Option<String>>>::new("text[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::text[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_varchar() {
        let test_table = TestTable::<FallbackToText>::new("varchar".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_varchar_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("varchar[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::varchar[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bpchar() {
        let test_table = TestTable::<FallbackToText>::new("bpchar".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bpchar_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("bpchar[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::bpchar[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_name() {
        let test_table = TestTable::<FallbackToText>::new("name".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('asd'), ('e'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_name_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("name[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['asd','efg',null]), (array['e']), (null), (array[]::name[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_enum() {
        let create_enum_query = "CREATE TYPE color AS ENUM ('red', 'green', 'blue');";
        Spi::run(create_enum_query).unwrap();

        let test_table = TestTable::<FallbackToText>::new("color".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('red'), ('blue'), ('green'), (null);");
        test_helper(test_table);

        let drop_enum_query = "DROP TYPE color CASCADE;";
        Spi::run(drop_enum_query).unwrap();
    }

    #[pg_test]
    fn test_enum_array() {
        let create_enum_query = "CREATE TYPE color AS ENUM ('red', 'green', 'blue');";
        Spi::run(create_enum_query).unwrap();

        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("color[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['red','blue','green',null]::color[]), (array['blue']::color[]), (null), (array[]::color[]);");
        test_helper(test_table);

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
        test_helper(test_table);

        let drop_enum_query = "DROP TYPE color CASCADE;";
        Spi::run(drop_enum_query).unwrap();
    }

    #[pg_test]
    fn test_bit() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('1'), ('1'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bit_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("bit[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['1','0','1']::bit[]), (array['1']::bit[]), (null), (array[]::bit[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "\"a\" is not a valid binary digit")]
    fn test_bit_invalid_value() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('a');");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "bit string length 2 does not match type bit(1)")]
    fn test_bit_invalid_length() {
        let test_table = TestTable::<FallbackToText>::new("bit".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('01');");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_varbit() {
        let test_table = TestTable::<FallbackToText>::new("varbit".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('0101'), ('1'), ('1111110010101'), (null);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_varbit_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("varbit[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['0101','1','1111110010101',null]::varbit[]), (null), (array[]::varbit[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_char() {
        let test_table = TestTable::<i8>::new("\"char\"".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('a'), ('b'), ('c'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_char_array() {
        let test_table = TestTable::<Vec<Option<i8>>>::new("\"char\"[]".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES (array['a','b','c',null]), (null), (array[]::\"char\"[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bytea() {
        let test_table = TestTable::<Vec<u8>>::new("bytea".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (E'\\\\x010203'), (E'\\\\x040506'), (null);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_bytea_array() {
        let test_table = TestTable::<pgrx::Array<&[u8]>>::new("bytea[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[E'\\\\x010203',E'\\\\x040506',null]::bytea[]), (null), (array[]::bytea[]);",
        );
        let TestResult { expected, result } = test_common(test_table);

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
        test_helper(test_table);
    }

    #[pg_test]
    fn test_oid_array() {
        let test_table = TestTable::<Vec<Option<Oid>>>::new("oid[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array[1,2,null]), (null), (array[]::oid[]);",
        );
        test_helper(test_table);
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

        let TestResult { expected, result } = test_common(test_table);

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

        let TestResult { expected, result } = test_common(test_table);

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

        let copy_to = "COPY (SELECT a FROM test_table) TO '/tmp/test.parquet'";
        Spi::run(copy_to).unwrap();
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

        let copy_to = "COPY (SELECT a FROM test_table) TO '/tmp/test.parquet'";
        Spi::run(copy_to).unwrap();
    }

    #[pg_test]
    fn test_date() {
        let test_table = TestTable::<Date>::new("date".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('2022-05-01'), ('2022-05-02'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_date_array() {
        let test_table = TestTable::<Vec<Option<Date>>>::new("date[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['2022-05-01','2022-05-02',null]::date[]), (null), (array[]::date[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_time() {
        let test_table = TestTable::<Time>::new("time".into());
        test_table
            .insert("INSERT INTO test_expected (a) VALUES ('15:00:00'), ('15:30:12'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_time_array() {
        let test_table = TestTable::<Vec<Option<Time>>>::new("time[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['15:00:00','15:30:12',null]::time[]), (null), (array[]::time[]);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_timetz() {
        let test_table = TestTable::<TimeWithTimeZone>::new("timetz".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('15:00:00+03'), ('15:30:12-03'), (null);",
        );
        let TestResult { expected, result } = test_common(test_table);

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

        test_assert(expected, result);
    }

    #[pg_test]
    fn test_timetz_array() {
        let test_table = TestTable::<Vec<Option<TimeWithTimeZone>>>::new("timetz[]".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (array['15:00:00+03','15:30:12-03',null]::timetz[]), (null), (array[]::timetz[]);",
        );
        let TestResult { expected, result } = test_common(test_table);

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

        test_assert(expected, result);
    }

    #[pg_test]
    fn test_timestamp() {
        let test_table = TestTable::<Timestamp>::new("timestamp".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES ('2022-05-01 15:00:00'), ('2022-05-02 15:30:12'), (null);",
        );
        test_helper(test_table);
    }

    #[pg_test]
    fn test_timestamp_array() {
        let test_table = TestTable::<Vec<Option<Timestamp>>>::new("timestamp[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['2022-05-01 15:00:00','2022-05-02 15:30:12',null]::timestamp[]), (null), (array[]::timestamp[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_timestamptz() {
        let test_table = TestTable::<TimestampWithTimeZone>::new("timestamptz".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('2022-05-01 15:00:00+03'), ('2022-05-02 15:30:12-03'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_timestamptz_array() {
        let test_table =
            TestTable::<Vec<Option<TimestampWithTimeZone>>>::new("timestamptz[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['2022-05-01 15:00:00+03','2022-05-02 15:30:12-03',null]::timestamptz[]), (null), (array[]::timestamptz[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_interval() {
        let test_table = TestTable::<FallbackToText>::new("interval".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('15 years 10 months 1 day 10:00:00'), ('5 days 4 minutes 10 seconds'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_interval_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("interval[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['15 years 10 months 1 day 10:00:00','5 days 4 minutes 10 seconds',null]::interval[]), (null), (array[]::interval[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_uuid() {
        let test_table = TestTable::<FallbackToText>::new("uuid".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('00000000-0000-0000-0000-000000000001'), ('00000000-0000-0000-0000-000000000002'), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_uuid_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("uuid[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['00000000-0000-0000-0000-000000000001','00000000-0000-0000-0000-000000000002',null]::uuid[]), (null), (array[]::uuid[]);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_json() {
        let test_table =
            TestTable::<FallbackToText>::new("json".into()).with_order_by_col("a->>'a'".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('{\"a\":\"test_json_1\"}'), ('{\"a\":\"test_json_2\"}'), (null);");
        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            assert_eq!(expected, actual);
        }
    }

    #[pg_test]
    fn test_json_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("json[]".into())
            .with_order_by_col("a::text[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['{\"a\":\"test_json_1\"}','{\"a\":\"test_json_2\"}',null]::json[]), (null), (array[]::json[]);");
        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            assert_eq!(expected, actual);
        }
    }

    #[pg_test]
    fn test_jsonb() {
        let test_table =
            TestTable::<FallbackToText>::new("jsonb".into()).with_order_by_col("a->>'a'".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES ('{\"a\":\"test_jsonb_1\"}'), ('{\"a\":\"test_jsonb_2\"}'), (null);");
        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            assert_eq!(expected, actual);
        }
    }

    #[pg_test]
    fn test_jsonb_array() {
        let test_table = TestTable::<Vec<Option<FallbackToText>>>::new("jsonb[]".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (array['{\"a\":\"test_jsonb_1\"}','{\"a\":\"test_jsonb_2\"}',null]::jsonb[]), (null), (array[]::jsonb[]);");
        let TestResult { expected, result } = test_common(test_table);

        for ((expected,), (actual,)) in expected.into_iter().zip(result.into_iter()) {
            assert_eq!(expected, actual);
        }
    }

    #[pg_test]
    fn test_small_numeric() {
        let attribute_schema_getter = || -> Vec<(Option<i32>, Option<i32>, String, String)> {
            Spi::connect(|client| {
                let parquet_schema_command = "select precision, scale, logical_type, type_name from parquet.schema('/tmp/test.parquet') WHERE name = 'a';";

                let tup_table = client.select(parquet_schema_command, None, None).unwrap();
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
        test_helper(test_table);

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
        test_helper(test_table);

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
        test_helper(test_table);

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
        test_helper(test_table);
    }

    #[pg_test]
    fn test_large_numeric() {
        let large_precision = DEFAULT_UNBOUNDED_NUMERIC_PRECISION + 1;

        let test_table =
            TestTable::<FallbackToText>::new(format!("numeric({},4)", large_precision));
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.020), (2.12313), (3), (null);",
        );
        test_helper(test_table);
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
        test_helper(test_table);
    }

    #[pg_test]
    fn test_unbounded_numeric() {
        let test_table = TestTable::<AnyNumeric>::new("numeric".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (0.0), (.0), (1.), (+1.02), (2.12), (3), (null);",
        );
        test_helper(test_table);

        let parquet_schema_command =
            "select precision, scale, logical_type, type_name from parquet.schema('/tmp/test.parquet') WHERE name = 'a';";

        let attribute_schema = Spi::connect(|client| {
            let tup_table = client.select(parquet_schema_command, None, None).unwrap();
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
        test_helper(test_table);

        let parquet_schema_command =
            "select precision, scale, logical_type, type_name from parquet.schema('/tmp/test.parquet') WHERE name = 'a' ORDER BY logical_type;";

        let attribute_schema = Spi::connect(|client| {
            let tup_table = client.select(parquet_schema_command, None, None).unwrap();
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

        assert_eq!(attribute_schema.len(), 2);
        assert_eq!(
            attribute_schema[0],
            (
                Some(DEFAULT_UNBOUNDED_NUMERIC_PRECISION as _),
                Some(DEFAULT_UNBOUNDED_NUMERIC_SCALE as _),
                "DECIMAL".to_string(),
                Some("FIXED_LEN_BYTE_ARRAY".to_string())
            )
        );
        assert_eq!(attribute_schema[1], (None, None, "LIST".to_string(), None));
    }

    #[pg_test]
    #[should_panic(
        expected = "numeric value contains 23 digits before decimal point, which exceeds max allowed integral digits 22 during copy to parquet"
    )]
    fn test_invalid_unbounded_numeric_integral_digits() {
        let invalid_integral_digits =
            DEFAULT_UNBOUNDED_NUMERIC_PRECISION - DEFAULT_UNBOUNDED_NUMERIC_SCALE + 1;

        let copy_to_command = format!(
            "copy (select (repeat('1', {}) || '.2')::numeric as a) to '/tmp/test.parquet'",
            invalid_integral_digits
        );

        Spi::run(&copy_to_command).unwrap();
    }

    #[pg_test]
    #[should_panic(
        expected = "numeric value contains 17 digits after decimal point, which exceeds max allowed decimal digits 16 during copy to parquet"
    )]
    fn test_invalid_unbounded_numeric_decimal_digits() {
        let invalid_decimal_digits = DEFAULT_UNBOUNDED_NUMERIC_SCALE + 1;

        let copy_to_command = format!(
            "copy (select ('2.' || repeat('1', {}) )::numeric as a) to '/tmp/test.parquet'",
            invalid_decimal_digits
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
        test_helper(test_table);
    }

    #[cfg(not(feature = "pg14"))]
    #[pg_test]
    fn test_numeric_negative_scale() {
        let test_table = TestTable::<AnyNumeric>::new("numeric(5,-2)".into());
        test_table.insert(
            "INSERT INTO test_expected (a) VALUES (1234567.1231244), (123.23223), (12.0), (-12.12303), (null), (0);",
        );
        test_helper(test_table);
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
        test_helper(test_table);
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
        test_helper(test_table);
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
            let tup_table = client.select(select_command, None, None).unwrap();

            for row in tup_table {
                let owner = row["owner"].value::<composite_type!("dog_owner")>();
                results.push(owner.unwrap());
            }

            results
        });

        Spi::run("TRUNCATE dog_owners;").unwrap();

        let uri = "/tmp/test.parquet";

        let copy_to_query = format!(
            "COPY (SELECT owner FROM dog_owners) TO '{}' WITH (format parquet);",
            uri
        );
        Spi::run(copy_to_query.as_str()).unwrap();

        Spi::run("TRUNCATE dog_owners;").unwrap();

        let copy_from_query = format!("COPY dog_owners FROM '{}' WITH (format parquet);", uri);
        Spi::run(copy_from_query.as_str()).unwrap();

        let result = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, None).unwrap();

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

    #[pg_test]
    fn test_copy_with_empty_options() {
        let test_table = TestTable::<i32>::new("int4".into())
            .with_copy_to_options(HashMap::new())
            .with_copy_from_options(HashMap::new());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_with_generated_and_dropped_columns() {
        Spi::run("DROP TABLE IF EXISTS test;").unwrap();

        Spi::run("CREATE TABLE test (a int, b int generated always as (10) stored, c text);")
            .unwrap();

        Spi::run("ALTER TABLE test DROP COLUMN a;").unwrap();

        Spi::run("INSERT INTO test (c) VALUES ('test');").unwrap();

        let uri = "/tmp/test.parquet";

        let copy_to_query = format!(
            "COPY (SELECT * FROM test) TO '{}' WITH (format parquet);",
            uri
        );
        Spi::run(copy_to_query.as_str()).unwrap();

        let expected = vec![(Some(10), Some("test".to_string()))];

        Spi::run("TRUNCATE test;").unwrap();

        let copy_from_query = format!("COPY test FROM '{}' WITH (format parquet);", uri);
        Spi::run(copy_from_query.as_str()).unwrap();

        let select_command = "SELECT b, c FROM test ORDER BY b, c;";
        let result = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, None).unwrap();

            for row in tup_table {
                let b = row["b"].value::<i32>();
                let c = row["c"].value::<String>();
                results.push((b.unwrap(), c.unwrap()));
            }

            results
        });

        for (expected, actual) in expected.into_iter().zip(result.into_iter()) {
            assert_eq!(expected.0, actual.0);
            assert_eq!(expected.1, actual.1);
        }
    }

    #[pg_test]
    fn test_compression_from_option() {
        let compression_options = vec![
            PgParquetCompression::Uncompressed,
            PgParquetCompression::Snappy,
            PgParquetCompression::Gzip,
            PgParquetCompression::Brotli,
            PgParquetCompression::Lz4,
            PgParquetCompression::Lz4raw,
            PgParquetCompression::Zstd,
        ];

        let expected_compression = vec![
            "UNCOMPRESSED",
            "SNAPPY",
            "GZIP(GzipLevel(6))",
            "BROTLI(BrotliLevel(1))",
            "LZ4",
            "LZ4_RAW",
            "ZSTD(ZstdLevel(1))",
        ];

        for (compression_option, expected_compression) in
            compression_options.into_iter().zip(expected_compression)
        {
            let mut copy_options = HashMap::new();
            copy_options.insert(
                "compression".to_string(),
                CopyOptionValue::StringOption(compression_option.to_string()),
            );

            let test_table =
                TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
            test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
            test_helper(test_table);

            let parquet_metadata_command =
                "select compression from parquet.metadata('/tmp/test.parquet');";

            let result_compression = Spi::get_one::<String>(parquet_metadata_command)
                .unwrap()
                .unwrap();

            assert_eq!(expected_compression, result_compression);
        }
    }

    #[pg_test]
    fn test_compression_from_uri() {
        let parquet_uris = vec![
            "/tmp/test.parquet",
            "/tmp/test.parquet.snappy",
            "/tmp/test.parquet.gz",
            "/tmp/test.parquet.br",
            "/tmp/test.parquet.lz4",
            "/tmp/test.parquet.zst",
        ];

        let expected_compression = vec![
            "SNAPPY",
            "SNAPPY",
            "GZIP(GzipLevel(6))",
            "BROTLI(BrotliLevel(1))",
            "LZ4",
            "ZSTD(ZstdLevel(1))",
        ];

        for (uri, expected_compression) in parquet_uris.into_iter().zip(expected_compression) {
            let test_table = TestTable::<i32>::new("int4".into()).with_uri(uri.to_string());
            test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
            test_helper(test_table);

            let parquet_metadata_command =
                format!("select compression from parquet.metadata('{}');", uri);

            let result_compression = Spi::get_one::<String>(&parquet_metadata_command)
                .unwrap()
                .unwrap();

            assert_eq!(expected_compression, result_compression);
        }
    }

    #[pg_test]
    fn test_s3_object_store_from_env() {
        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_s3_object_store_from_config_file() {
        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        // remove these to make sure the config file is used
        let access_key_id = std::env::var("AWS_ACCESS_KEY_ID").unwrap();
        std::env::remove_var("AWS_ACCESS_KEY_ID");
        let secret_access_key = std::env::var("AWS_SECRET_ACCESS_KEY").unwrap();
        std::env::remove_var("AWS_SECRET_ACCESS_KEY");
        let region = std::env::var("AWS_REGION").unwrap();
        std::env::remove_var("AWS_REGION");

        // create a config file
        let aws_config_file_content = format!(
            "[profile pg_parquet_test]\nregion = {}\naws_access_key_id = {}\naws_secret_access_key = {}\n",
            region, access_key_id, secret_access_key
        );
        std::env::set_var("AWS_PROFILE", "pg_parquet_test");

        let aws_config_file = "/tmp/aws_config";
        std::env::set_var("AWS_CONFIG_FILE", aws_config_file);

        let mut aws_config_file = std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(aws_config_file)
            .unwrap();

        aws_config_file
            .write_all(aws_config_file_content.as_bytes())
            .unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY from a remote uri")]
    fn test_s3_no_read_access() {
        // create regular user
        Spi::run("CREATE USER regular_user;").unwrap();

        // grant write access to the regular user but not read access
        Spi::run("GRANT parquet_object_store_write TO regular_user;").unwrap();

        // grant all permissions for public schema
        Spi::run("GRANT ALL ON SCHEMA public TO regular_user;").unwrap();

        // set the current user to the regular user
        Spi::run("SET SESSION AUTHORIZATION regular_user;").unwrap();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");

        // can write to s3
        let copy_to_command = format!(
            "COPY (SELECT a FROM generate_series(1,10) a) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();

        // cannot read from s3
        let copy_from_command = format!("COPY test_expected FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY to a remote uri")]
    fn test_s3_no_write_access() {
        // create regular user
        Spi::run("CREATE USER regular_user;").unwrap();

        // grant read access to the regular user but not write access
        Spi::run("GRANT parquet_object_store_read TO regular_user;").unwrap();

        // grant usage access to parquet schema and its udfs
        Spi::run("GRANT USAGE ON SCHEMA parquet TO regular_user;").unwrap();
        Spi::run("GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA parquet TO regular_user;").unwrap();

        // grant all permissions for public schema
        Spi::run("GRANT ALL ON SCHEMA public TO regular_user;").unwrap();

        // set the current user to the regular user
        Spi::run("SET SESSION AUTHORIZATION regular_user;").unwrap();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        // can call metadata udf (requires read access)
        let metadata_query = format!("SELECT parquet.metadata('{}');", s3_uri.clone());
        Spi::run(&metadata_query).unwrap();

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");

        // can read from s3
        let copy_from_command = format!("COPY test_expected FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();

        // cannot write to s3
        let copy_to_command = format!(
            "COPY (SELECT a FROM generate_series(1,10) a) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "404 Not Found")]
    fn test_s3_object_store_write_invalid_uri() {
        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "404 Not Found")]
    fn test_s3_object_store_read_invalid_uri() {
        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let create_table_command = "CREATE TABLE test_table (a int);";
        Spi::run(create_table_command).unwrap();

        let copy_from_command = format!("COPY test_table FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "unsupported uri gs://testbucket")]
    fn test_unsupported_uri() {
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("gs://testbucket".to_string());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "invalid_format is not a valid format")]
    fn test_invalid_format_copy_from() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "format".to_string(),
            CopyOptionValue::StringOption("invalid_format".to_string()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_from_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "nonexisted is not a valid option for \"copy from parquet\".")]
    fn test_nonexistent_copy_from_option() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "nonexisted".to_string(),
            CopyOptionValue::StringOption("nonexisted".to_string()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_from_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "nonexisted is not a valid option for \"copy to parquet\".")]
    fn test_nonexistent_copy_to_option() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "nonexisted".to_string(),
            CopyOptionValue::StringOption("nonexisted".to_string()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "invalid_format is not a valid format")]
    fn test_invalid_format_copy_to() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "format".to_string(),
            CopyOptionValue::StringOption("invalid_format".to_string()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "invalid_compression is not a valid compression format")]
    fn test_invalid_compression() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "compression".to_string(),
            CopyOptionValue::StringOption("invalid_compression".to_string()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "compression level is not supported for \"snappy\" compression")]
    fn test_unsupported_compression_level_with_snappy_option() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "compression".to_string(),
            CopyOptionValue::StringOption("snappy".to_string()),
        );
        copy_options.insert(
            "compression_level".to_string(),
            CopyOptionValue::IntOption(1),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "compression level is not supported for \"snappy\" compression")]
    fn test_unsupported_compression_level_with_snappy_file() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "compression_level".to_string(),
            CopyOptionValue::IntOption(1),
        );

        let test_table = TestTable::<i32>::new("int4".into())
            .with_copy_to_options(copy_options)
            .with_uri("/tmp/test.parquet.snappy".to_string());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "valid compression range 0..=10 exceeded")]
    fn test_invalid_gzip_compression_level() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "compression".to_string(),
            CopyOptionValue::StringOption("gzip".to_string()),
        );
        copy_options.insert(
            "compression_level".to_string(),
            CopyOptionValue::IntOption(20),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_valid_gzip_compression_level() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "compression".to_string(),
            CopyOptionValue::StringOption("gzip".to_string()),
        );
        copy_options.insert(
            "compression_level".to_string(),
            CopyOptionValue::IntOption(1),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);

        let parquet_metadata_command =
            "select compression from parquet.metadata('/tmp/test.parquet');";

        let result_compression = Spi::get_one::<String>(parquet_metadata_command)
            .unwrap()
            .unwrap();

        // compression level is not read properly by parquet-rs (bug)
        assert!(result_compression.starts_with("GZIP"));
    }

    #[pg_test]
    #[should_panic(expected = "row_group_size must be greater than 0")]
    fn test_invalid_row_group_size() {
        let mut copy_options = HashMap::new();
        copy_options.insert("row_group_size".to_string(), CopyOptionValue::IntOption(-1));

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    #[should_panic(expected = "row_group_size_bytes must be greater than 0")]
    fn test_invalid_row_group_size_bytes() {
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "row_group_size_bytes".to_string(),
            CopyOptionValue::IntOption(-1),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_helper(test_table);
    }

    #[pg_test]
    fn test_nested_copy_to_stmts() {
        let create_func_command = "
            CREATE OR REPLACE FUNCTION copy_to(url text)
            RETURNS text
            LANGUAGE plpgsql
            AS $function$
            DECLARE
            BEGIN
                EXECUTE format($$COPY (SELECT s FROM generate_series(1,3) s) TO %L WITH (format 'parquet')$$, url);
                RETURN 'success';
            END;
            $function$;
        ";
        Spi::run(create_func_command).unwrap();

        let create_table_command = "CREATE TABLE exports (id int, url text);";
        Spi::run(create_table_command).unwrap();

        let insert_query =
            "insert into exports values ( 1, '/tmp/test1.parquet'), ( 2, '/tmp/test2.parquet');";
        Spi::run(insert_query).unwrap();

        let nested_copy_command =
            "COPY (SELECT copy_to(url) as copy_to_result FROM exports) TO '/tmp/test3.parquet';";
        Spi::run(nested_copy_command).unwrap();

        let create_table_command = "
            CREATE TABLE file1_result (s int);
            CREATE TABLE file3_result (copy_to_result text);
        ";
        Spi::run(create_table_command).unwrap();

        let copy_from_command = "
            COPY file1_result FROM '/tmp/test1.parquet';
            COPY file3_result FROM '/tmp/test3.parquet';
        ";
        Spi::run(copy_from_command).unwrap();

        let select_command = "SELECT * FROM file1_result ORDER BY s;";
        let result1 = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, None).unwrap();

            for row in tup_table {
                let s = row["s"].value::<i32>();
                results.push(s.unwrap().unwrap());
            }

            results
        });

        assert_eq!(vec![1, 2, 3], result1);

        let select_command = "SELECT * FROM file3_result;";
        let result3 = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(select_command, None, None).unwrap();

            for row in tup_table {
                let copy_to_result = row["copy_to_result"].value::<&str>();
                results.push(copy_to_result.unwrap().unwrap());
            }

            results
        });

        assert_eq!(vec!["success"; 2], result3);
    }

    #[pg_test]
    fn test_parquet_schema() {
        let ddls = "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            copy workers to '/tmp/test.parquet';
        ";
        Spi::run(ddls).unwrap();

        let parquet_schema_command =
            "select * from parquet.schema('/tmp/test.parquet') ORDER BY name, converted_type;";

        let result_schema = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(parquet_schema_command, None, None).unwrap();

            for row in tup_table {
                let uri = row["uri"].value::<String>().unwrap().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();
                let type_name = row["type_name"].value::<String>().unwrap();
                let type_length = row["type_length"].value::<String>().unwrap();
                let repetition_type = row["repetition_type"].value::<String>().unwrap();
                let num_children = row["num_children"].value::<i32>().unwrap();
                let converted_type = row["converted_type"].value::<String>().unwrap();
                let scale = row["scale"].value::<i32>().unwrap();
                let precision = row["precision"].value::<i32>().unwrap();
                let field_id = row["field_id"].value::<i32>().unwrap();
                let logical_type = row["logical_type"].value::<String>().unwrap();

                results.push((
                    uri,
                    name,
                    type_name,
                    type_length,
                    repetition_type,
                    num_children,
                    converted_type,
                    scale,
                    precision,
                    field_id,
                    logical_type,
                ));
            }

            results
        });

        let expected_schema = vec![
            (
                "/tmp/test.parquet".into(),
                "arrow_schema".into(),
                None,
                None,
                None,
                Some(3),
                None,
                None,
                None,
                None,
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "company".into(),
                Some("BYTE_ARRAY".into()),
                None,
                Some("OPTIONAL".into()),
                None,
                Some("UTF8".into()),
                None,
                None,
                Some(8),
                Some("STRING".into()),
            ),
            (
                "/tmp/test.parquet".into(),
                "id".into(),
                Some("INT32".into()),
                None,
                Some("OPTIONAL".into()),
                None,
                None,
                None,
                None,
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "id".into(),
                Some("INT32".into()),
                None,
                Some("OPTIONAL".into()),
                None,
                None,
                None,
                None,
                Some(0),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "list".into(),
                None,
                None,
                Some("REPEATED".into()),
                Some(1),
                None,
                None,
                None,
                None,
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "list".into(),
                None,
                None,
                Some("REPEATED".into()),
                Some(1),
                None,
                None,
                None,
                None,
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "monthly_salary".into(),
                Some("INT64".into()),
                None,
                Some("OPTIONAL".into()),
                None,
                Some("DECIMAL".into()),
                Some(6),
                Some(15),
                Some(7),
                Some("DECIMAL".into()),
            ),
            (
                "/tmp/test.parquet".into(),
                "name".into(),
                Some("BYTE_ARRAY".into()),
                None,
                Some("OPTIONAL".into()),
                None,
                Some("UTF8".into()),
                None,
                None,
                Some(6),
                Some("STRING".into()),
            ),
            (
                "/tmp/test.parquet".into(),
                "p".into(),
                None,
                None,
                Some("OPTIONAL".into()),
                Some(1),
                Some("LIST".into()),
                None,
                None,
                Some(3),
                Some("LIST".into()),
            ),
            (
                "/tmp/test.parquet".into(),
                "p".into(),
                None,
                None,
                Some("OPTIONAL".into()),
                Some(2),
                None,
                None,
                None,
                Some(4),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                "workers".into(),
                None,
                None,
                Some("OPTIONAL".into()),
                Some(1),
                Some("LIST".into()),
                None,
                None,
                Some(1),
                Some("LIST".into()),
            ),
            (
                "/tmp/test.parquet".into(),
                "workers".into(),
                None,
                None,
                Some("OPTIONAL".into()),
                Some(2),
                None,
                None,
                None,
                Some(2),
                None,
            ),
        ];

        assert_eq!(result_schema, expected_schema);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }

    #[pg_test]
    fn test_parquet_metadata() {
        let total_rows = 10;
        let row_group_size = 5;

        let ddls = format!(
            "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            insert into workers select i, null::worker[], null from generate_series(1, {}) i;
            copy workers to '/tmp/test.parquet' with (row_group_size {});
        ",
            total_rows, row_group_size
        );
        Spi::run(&ddls).unwrap();

        let parquet_metadata_command = "select * from parquet.metadata('/tmp/test.parquet');";

        // Debug (assert_eq! requires) is only implemented for tuples up to 12 elements. This is why we split the
        // metadata into two parts.
        let (result_metadata_part1, result_metadata_part2) = Spi::connect(|client| {
            let mut results_part1 = Vec::new();
            let mut results_part2 = Vec::new();

            let tup_table = client.select(parquet_metadata_command, None, None).unwrap();

            for row in tup_table {
                let uri = row["uri"].value::<String>().unwrap().unwrap();
                let row_group_id = row["row_group_id"].value::<i64>().unwrap().unwrap();
                let row_group_num_rows = row["row_group_num_rows"].value::<i64>().unwrap().unwrap();
                let row_group_num_columns = row["row_group_num_columns"]
                    .value::<i64>()
                    .unwrap()
                    .unwrap();
                let row_group_bytes = row["row_group_bytes"].value::<i64>().unwrap().unwrap();
                let column_id = row["column_id"].value::<i64>().unwrap().unwrap();
                let file_offset = row["file_offset"].value::<i64>().unwrap().unwrap();
                let num_values = row["num_values"].value::<i64>().unwrap().unwrap();
                let path_in_schema = row["path_in_schema"].value::<String>().unwrap().unwrap();
                let type_name = row["type_name"].value::<String>().unwrap().unwrap();
                let stats_null_count = row["stats_null_count"].value::<i64>().unwrap();
                let stats_distinct_count = row["stats_distinct_count"].value::<i64>().unwrap();

                let stats_min = row["stats_min"].value::<String>().unwrap();
                let stats_max = row["stats_max"].value::<String>().unwrap();
                let compression = row["compression"].value::<String>().unwrap().unwrap();
                let encodings = row["encodings"].value::<String>().unwrap().unwrap();
                let index_page_offset = row["index_page_offset"].value::<i64>().unwrap();
                let dictionary_page_offset = row["dictionary_page_offset"].value::<i64>().unwrap();
                let data_page_offset = row["data_page_offset"].value::<i64>().unwrap().unwrap();
                let total_compressed_size = row["total_compressed_size"]
                    .value::<i64>()
                    .unwrap()
                    .unwrap();
                let total_uncompressed_size = row["total_uncompressed_size"]
                    .value::<i64>()
                    .unwrap()
                    .unwrap();

                results_part1.push((
                    uri,
                    row_group_id,
                    row_group_num_rows,
                    row_group_num_columns,
                    row_group_bytes,
                    column_id,
                    file_offset,
                    num_values,
                    path_in_schema,
                    type_name,
                    stats_null_count,
                    stats_distinct_count,
                ));

                results_part2.push((
                    stats_min,
                    stats_max,
                    compression,
                    encodings,
                    index_page_offset,
                    dictionary_page_offset,
                    data_page_offset,
                    total_compressed_size,
                    total_uncompressed_size,
                ));
            }

            (results_part1, results_part2)
        });

        let expected_metadata_part1 = vec![
            (
                "/tmp/test.parquet".into(),
                0,
                5,
                5,
                250,
                0,
                0,
                5,
                "id".into(),
                "INT32".into(),
                Some(0),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                0,
                5,
                5,
                250,
                1,
                0,
                5,
                "workers.list.workers.p.list.p.id".into(),
                "INT32".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                0,
                5,
                5,
                250,
                2,
                0,
                5,
                "workers.list.workers.p.list.p.name".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                0,
                5,
                5,
                250,
                3,
                0,
                5,
                "workers.list.workers.monthly_salary".into(),
                "INT64".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                0,
                5,
                5,
                250,
                4,
                0,
                5,
                "company".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                1,
                5,
                5,
                250,
                0,
                0,
                5,
                "id".into(),
                "INT32".into(),
                Some(0),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                1,
                5,
                5,
                250,
                1,
                0,
                5,
                "workers.list.workers.p.list.p.id".into(),
                "INT32".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                1,
                5,
                5,
                250,
                2,
                0,
                5,
                "workers.list.workers.p.list.p.name".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                1,
                5,
                5,
                250,
                3,
                0,
                5,
                "workers.list.workers.monthly_salary".into(),
                "INT64".into(),
                Some(5),
                None,
            ),
            (
                "/tmp/test.parquet".into(),
                1,
                5,
                5,
                250,
                4,
                0,
                5,
                "company".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
        ];

        let expected_metadata_part2 = vec![
            (
                Some("1".into()),
                Some("5".into()),
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(4),
                40,
                84,
                80,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(88),
                103,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(135),
                150,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(182),
                197,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(229),
                244,
                41,
                38,
            ),
            (
                Some("6".into()),
                Some("10".into()),
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(270),
                306,
                84,
                80,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(354),
                369,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(401),
                416,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(448),
                463,
                47,
                44,
            ),
            (
                None,
                None,
                "SNAPPY".into(),
                "PLAIN,RLE,RLE_DICTIONARY".into(),
                None,
                Some(495),
                510,
                41,
                38,
            ),
        ];

        assert_eq!(result_metadata_part1, expected_metadata_part1);
        assert_eq!(result_metadata_part2, expected_metadata_part2);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }

    #[pg_test]
    fn test_parquet_file_metadata() {
        let total_rows = 10;
        let row_group_size = 2;
        let total_row_groups = total_rows / row_group_size;

        let ddls = format!(
            "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            insert into workers select i, null::worker[], null from generate_series(1, {}) i;
            copy workers to '/tmp/test.parquet' with (row_group_size {});
        ",
            total_rows, row_group_size
        );
        Spi::run(&ddls).unwrap();

        let parquet_file_metadata_command =
            "select * from parquet.file_metadata('/tmp/test.parquet');";

        let result_file_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(parquet_file_metadata_command, None, None)
                .unwrap();

            for row in tup_table {
                let uri = row["uri"].value::<String>().unwrap().unwrap();
                let created_by = row["created_by"].value::<String>().unwrap();
                let num_rows = row["num_rows"].value::<i64>().unwrap().unwrap();
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                let format_version = row["format_version"].value::<String>().unwrap().unwrap();

                results.push((uri, created_by, num_rows, num_row_groups, format_version));
            }

            results
        });

        let expected_file_metadata = vec![(
            "/tmp/test.parquet".into(),
            Some("pg_parquet".into()),
            total_rows,
            total_row_groups,
            "1".into(),
        )];

        assert_eq!(result_file_metadata, expected_file_metadata);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }

    #[pg_test]
    fn test_parquet_kv_metadata() {
        let ddls = "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            copy workers to '/tmp/test.parquet';
        ";
        Spi::run(ddls).unwrap();

        let parquet_kv_metadata_command = "select * from parquet.kv_metadata('/tmp/test.parquet');";

        let result_kv_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(parquet_kv_metadata_command, None, None)
                .unwrap();

            for row in tup_table {
                let uri = row["uri"].value::<String>().unwrap().unwrap();
                let key = row["key"].value::<Vec<u8>>().unwrap().unwrap();
                let value = row["value"].value::<Vec<u8>>().unwrap();

                results.push((uri, key, value));
            }

            results
        });

        let expected_kv_metadata = vec![(
            "/tmp/test.parquet".into(),
            vec![65, 82, 82, 79, 87, 58, 115, 99, 104, 101, 109, 97],
            Some(vec![
                47, 47, 47, 47, 47, 43, 103, 68, 65, 65, 65, 81, 65, 65, 65, 65, 65, 65, 65, 75,
                65, 65, 119, 65, 67, 103, 65, 74, 65, 65, 81, 65, 67, 103, 65, 65, 65, 66, 65, 65,
                65, 65, 65, 65, 65, 81, 81, 65, 67, 65, 65, 73, 65, 65, 65, 65, 66, 65, 65, 73, 65,
                65, 65, 65, 66, 65, 65, 65, 65, 65, 77, 65, 65, 65, 66, 69, 65, 119, 65, 65, 97,
                65, 65, 65, 65, 65, 81, 65, 65, 65, 68, 97, 47, 80, 47, 47, 75, 65, 65, 65, 65, 66,
                81, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 66, 81, 119, 65, 65, 65,
                65, 65, 65, 65, 65, 65, 109, 80, 55, 47, 47, 119, 99, 65, 65, 65, 66, 106, 98, 50,
                49, 119, 89, 87, 53, 53, 65, 65, 69, 65, 65, 65, 65, 69, 65, 65, 65, 65, 118, 80,
                122, 47, 47, 119, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 81, 65, 65, 65, 68,
                103, 65, 65, 65, 65, 81, 65, 65, 65, 65, 85, 69, 70, 83, 85, 86, 86, 70, 86, 68,
                112, 109, 97, 87, 86, 115, 90, 70, 57, 112, 90, 65, 65, 65, 65, 65, 65, 54, 47,
                102, 47, 47, 107, 65, 73, 65, 65, 66, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65,
                65, 65, 66, 68, 72, 81, 67, 65, 65, 65, 66, 65, 65, 65, 65, 67, 65, 65, 65, 65, 80,
                122, 43, 47, 47, 57, 101, 47, 102, 47, 47, 76, 65, 73, 65, 65, 66, 119, 65, 65, 65,
                65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 68, 82, 65, 67, 65, 65, 65, 67, 65, 65, 65,
                65, 102, 65, 65, 65, 65, 65, 103, 65, 65, 65, 65, 107, 47, 47, 47, 47, 104, 118,
                51, 47, 47, 122, 103, 65, 65, 65, 65, 85, 65, 65, 65, 65, 68, 65, 65, 65, 65, 65,
                65, 65, 65, 81, 99, 85, 65, 65, 65, 65, 65, 65, 65, 65, 65, 70, 68, 57, 47, 47, 56,
                71, 65, 65, 65, 65, 68, 119, 65, 65, 65, 65, 52, 65, 65, 65, 66, 116, 98, 50, 53,
                48, 97, 71, 120, 53, 88, 51, 78, 104, 98, 71, 70, 121, 101, 81, 65, 65, 65, 81, 65,
                65, 65, 65, 81, 65, 65, 65, 66, 52, 47, 102, 47, 47, 67, 65, 65, 65, 65, 65, 119,
                65, 65, 65, 65, 66, 65, 65, 65, 65, 78, 119, 65, 65, 65, 66, 65, 65, 65, 65, 66,
                81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109, 90, 112, 90, 87, 120, 107, 88, 50,
                108, 107, 65, 65, 65, 65, 65, 80, 98, 57, 47, 47, 57, 85, 65, 81, 65, 65, 71, 65,
                65, 65, 65, 65, 119, 65, 65, 65, 65, 65, 65, 65, 69, 77, 80, 65, 69, 65, 65, 65,
                69, 65, 65, 65, 65, 73, 65, 65, 65, 65, 117, 80, 47, 47, 47, 120, 114, 43, 47, 47,
                47, 48, 65, 65, 65, 65, 72, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 65, 65, 65,
                69, 78, 51, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66, 119, 65, 65, 65, 65, 67, 65,
                65, 65, 65, 79, 68, 47, 47, 47, 57, 67, 47, 118, 47, 47, 76, 65, 65, 65, 65, 66,
                103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 66, 82, 65, 65, 65, 65,
                65, 65, 65, 65, 65, 65, 66, 65, 65, 69, 65, 65, 81, 65, 65, 65, 65, 69, 65, 65, 65,
                65, 98, 109, 70, 116, 90, 81, 65, 65, 65, 65, 65, 66, 65, 65, 65, 65, 66, 65, 65,
                65, 65, 67, 106, 43, 47, 47, 56, 73, 65, 65, 65, 65, 68, 65, 65, 65, 65, 65, 69,
                65, 65, 65, 65, 50, 65, 65, 65, 65, 69, 65, 65, 65, 65, 70, 66, 66, 85, 108, 70,
                86, 82, 86, 81, 54, 90, 109, 108, 108, 98, 71, 82, 102, 97, 87, 81, 65, 65, 65, 65,
                65, 112, 118, 55, 47, 47, 121, 119, 65, 65, 65, 65, 81, 65, 65, 65, 65, 71, 65, 65,
                65, 65, 65, 65, 65, 65, 81, 73, 85, 65, 65, 65, 65, 108, 80, 55, 47, 47, 121, 65,
                65, 65, 65, 65, 65, 65, 65, 65, 66, 65, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66,
                112, 90, 65, 65, 65, 65, 81, 65, 65, 65, 65, 81, 65, 65, 65, 67, 77, 47, 118, 47,
                47, 67, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 78, 81, 65,
                65, 65, 66, 65, 65, 65, 65, 66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109, 90,
                112, 90, 87, 120, 107, 88, 50, 108, 107, 65, 65, 65, 65, 65, 65, 69, 65, 65, 65,
                66, 119, 65, 65, 65, 65, 65, 81, 65, 65, 65, 65, 81, 65, 65, 65, 68, 73, 47, 118,
                47, 47, 67, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 78, 65,
                65, 65, 65, 66, 65, 65, 65, 65, 66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109,
                90, 112, 90, 87, 120, 107, 88, 50, 108, 107, 65, 65, 65, 65, 65, 65, 69, 65, 65,
                65, 66, 119, 65, 65, 65, 65, 65, 81, 65, 65, 65, 65, 81, 65, 65, 65, 65, 69, 47,
                47, 47, 47, 67, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 77,
                119, 65, 65, 65, 66, 65, 65, 65, 65, 66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79,
                109, 90, 112, 90, 87, 120, 107, 88, 50, 108, 107, 65, 65, 65, 65, 65, 65, 99, 65,
                65, 65, 66, 51, 98, 51, 74, 114, 90, 88, 74, 122, 65, 65, 69, 65, 65, 65, 65, 69,
                65, 65, 65, 65, 82, 80, 47, 47, 47, 119, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65,
                65, 81, 65, 65, 65, 68, 73, 65, 65, 65, 65, 81, 65, 65, 65, 65, 85, 69, 70, 83, 85,
                86, 86, 70, 86, 68, 112, 109, 97, 87, 86, 115, 90, 70, 57, 112, 90, 65, 65, 65, 65,
                65, 65, 72, 65, 65, 65, 65, 100, 50, 57, 121, 97, 50, 86, 121, 99, 119, 65, 66, 65,
                65, 65, 65, 66, 65, 65, 65, 65, 73, 84, 47, 47, 47, 56, 73, 65, 65, 65, 65, 68, 65,
                65, 65, 65, 65, 69, 65, 65, 65, 65, 120, 65, 65, 65, 65, 69, 65, 65, 65, 65, 70,
                66, 66, 85, 108, 70, 86, 82, 86, 81, 54, 90, 109, 108, 108, 98, 71, 82, 102, 97,
                87, 81, 65, 65, 66, 73, 65, 71, 65, 65, 85, 65, 66, 73, 65, 69, 119, 65, 73, 65,
                65, 65, 65, 68, 65, 65, 69, 65, 66, 73, 65, 65, 65, 65, 48, 65, 65, 65, 65, 71, 65,
                65, 65, 65, 67, 65, 65, 65, 65, 65, 65, 65, 65, 69, 67, 72, 65, 65, 65, 65, 65,
                103, 65, 68, 65, 65, 69, 65, 65, 115, 65, 67, 65, 65, 65, 65, 67, 65, 65, 65, 65,
                65, 65, 65, 65, 65, 66, 65, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66, 112, 90, 65,
                65, 65, 65, 81, 65, 65, 65, 65, 119, 65, 65, 65, 65, 73, 65, 65, 119, 65, 67, 65,
                65, 69, 65, 65, 103, 65, 65, 65, 65, 73, 65, 65, 65, 65, 68, 65, 65, 65, 65, 65,
                69, 65, 65, 65, 65, 119, 65, 65, 65, 65, 69, 65, 65, 65, 65, 70, 66, 66, 85, 108,
                70, 86, 82, 86, 81, 54, 90, 109, 108, 108, 98, 71, 82, 102, 97, 87, 81, 65, 65, 65,
                65, 65,
            ]),
        )];

        assert_eq!(result_kv_metadata, expected_kv_metadata);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "file size of 2 is less than footer")]
    fn test_disabled_hooks() {
        Spi::run("SET pg_parquet.enable_copy_hooks TO false;").unwrap();
        Spi::run("COPY (SELECT 1 as id) TO '/tmp/test.parquet'").unwrap();

        let parquet_metadata_command = "select * from parquet.metadata('/tmp/test.parquet');";
        Spi::run(parquet_metadata_command).unwrap();
    }

    #[pg_test]
    #[should_panic(
        expected = "type mismatch for column \"location\" between table and parquet file"
    )]
    fn test_type_mismatch_between_parquet_and_table() {
        let create_types = "create type dog as (name text, age int);
                            create type person as (id bigint, name text, dogs dog[]);
                            create type address as (loc text);";
        Spi::run(create_types).unwrap();

        let create_correct_table =
            "create table factory_correct (id bigint, workers person[], name text, location address);";
        Spi::run(create_correct_table).unwrap();

        let create_wrong_table =
            "create table factory_wrong (id bigint, workers person[], name text, location int);";
        Spi::run(create_wrong_table).unwrap();

        let copy_to_parquet = "copy (select 1::int8 as id,
                                        array[
                                            row(1, 'ali', array[row('lady', 4), NULL]::dog[])
                                        ]::person[] as workers,
                                        'Microsoft' as name,
                                        row('istanbul')::address as location
                                 from generate_series(1,10) i) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        // copy to correct table which matches the parquet schema
        let copy_to_correct_table = "copy factory_correct from '/tmp/test.parquet';";
        Spi::run(copy_to_correct_table).unwrap();

        // copy to wrong table which does not match the parquet schema
        let copy_to_wrong_table = "copy factory_wrong from '/tmp/test.parquet';";
        Spi::run(copy_to_wrong_table).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "column \"name\" is not found in parquet file")]
    fn test_missing_column_in_parquet() {
        let create_table = "create table test(id int, name text);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = "copy (select 100 as id) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_to_table = "copy test from '/tmp/test.parquet';";
        Spi::run(copy_to_table).unwrap();
    }

    #[pg_test]
    fn test_large_arrow_array_limit() {
        // disable row group size bytes limit
        let mut copy_options = HashMap::new();
        copy_options.insert(
            "row_group_size_bytes".to_string(),
            CopyOptionValue::IntOption(10_000_000_000),
        );

        let test_table = TestTable::<String>::new("text".into()).with_copy_to_options(copy_options);
        test_table.insert(
            "INSERT INTO test_expected select repeat('a', 52000000) from generate_series(1,42) i;",
        );
        test_helper(test_table);

        let parquet_file_metadata_command =
            "select * from parquet.file_metadata('/tmp/test.parquet');";
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(parquet_file_metadata_command, None, None)
                .unwrap();

            for row in tup_table {
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                results.push(num_row_groups);
            }

            results
        });

        assert_eq!(result_metadata, vec![2]);
    }

    #[pg_test]
    fn test_with_column_names() {
        let create_table = "create table test(id int, name text);";
        Spi::run(create_table).unwrap();

        let insert_data = "insert into test values (1, 'ali'), (2, 'veli');";
        Spi::run(insert_data).unwrap();

        let copy_to_parquet = "copy test(id, name) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_from_parquet = "copy test(id, name) from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "column \"nonexistent\" does not exist")]
    fn test_copy_to_with_nonexistent_column_names() {
        let create_table = "create table test(id int, name text);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = "copy test(nonexistent) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "column \"nonexistent\" of relation \"test\" does not exist")]
    fn test_copy_from_with_nonexistent_column_names() {
        let create_table = "create table test(id int, name text);";
        Spi::run(create_table).unwrap();

        let copy_from_parquet = "copy test(nonexistent) from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    fn test_with_where_clause() {
        let create_table = "create table test(id int);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet =
            "copy (select i as id from generate_series(1,5) i) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_from_parquet = "copy test from '/tmp/test.parquet' where id > 2;";
        Spi::run(copy_from_parquet).unwrap();

        let result = Spi::connect(|client| {
            let tup_table = client.select("select * from test;", None, None).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let id = row["id"].value::<i32>().unwrap().unwrap();
                results.push(id);
            }

            results
        });
        assert_eq!(result, vec![3, 4, 5]);
    }

    #[pg_test]
    #[should_panic(expected = "duplicate attribute \"a\" is not allowed in parquet schema")]
    fn test_with_duplicate_column_name() {
        Spi::run("copy (select 1 as a, 2 as a) to '/tmp/test.parquet';").unwrap();
    }

    #[pg_test]
    fn test_row_group_size() {
        let total_rows = 10;
        let row_group_size = 2;
        let total_row_groups = total_rows / row_group_size;

        let create_table = "create table test(id int);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = format!(
            "copy (select i as id from generate_series(1,{}) i) to '/tmp/test.parquet' with (row_group_size {});",
            total_rows, row_group_size
        );
        Spi::run(&copy_to_parquet).unwrap();

        let parquet_file_metadata_command =
            "select * from parquet.file_metadata('/tmp/test.parquet');";
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(parquet_file_metadata_command, None, None)
                .unwrap();

            for row in tup_table {
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                results.push(num_row_groups);
            }

            results
        });

        assert_eq!(result_metadata, vec![total_row_groups]);
    }

    #[pg_test]
    fn test_row_group_size_bytes() {
        let create_table = "create table test(id int, name text);";
        Spi::run(create_table).unwrap();

        let insert_data = "insert into test select i, 'a' from generate_series(1, 1000000) i;";
        Spi::run(insert_data).unwrap();

        let id_bytes = 4;
        let name_bytes = 1;
        let total_rows_size_bytes = (id_bytes + name_bytes) * 1_000_000;

        let row_group_size_bytes = total_rows_size_bytes / 10;

        let copy_to_parquet = format!(
            "copy test to '/tmp/test.parquet' with (row_group_size_bytes {});",
            row_group_size_bytes
        );
        Spi::run(&copy_to_parquet).unwrap();

        let parquet_file_metadata_command =
            "select * from parquet.file_metadata('/tmp/test.parquet');";
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(parquet_file_metadata_command, None, None)
                .unwrap();

            for row in tup_table {
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                results.push(num_row_groups);
            }

            results
        });

        assert_eq!(result_metadata, vec![10]);
    }

    #[pg_test]
    fn test_with_quoted_table_name() {
        let create_table = "create table \"test _ta23BLe\"(\"id _asdsadasd343d\" int);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = "copy \"test _ta23BLe\" to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_from_parquet = "copy \"test _ta23BLe\" from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    fn test_with_qualified_table_name() {
        let create_schema = "create schema test_schema;";
        Spi::run(create_schema).unwrap();

        let create_table = "create table test_schema.test_table (id int);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = "copy test_schema.test_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_from_parquet = "copy test_schema.test_table from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();

        // test the same with search_path
        let set_search_path = "set search_path to test_schema;";
        Spi::run(set_search_path).unwrap();

        let copy_to_parquet = "copy test_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let copy_from_parquet = "copy test_table from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    fn test_copy_with_program_not_hooked() {
        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let insert_data = "insert into test_table select i from generate_series(1, 10) i;";
        Spi::run(insert_data).unwrap();

        let copy_to_parquet = "copy test_table to program 'cat' with (format csv);";
        Spi::run(copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY to a file")]
    fn test_with_no_write_files_privilege() {
        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let create_role = "create role test_role;";
        Spi::run(create_role).unwrap();

        let grant_role = "grant ALL ON ALL TABLES IN SCHEMA public TO test_role;";
        Spi::run(grant_role).unwrap();

        let set_role = "set role test_role;";
        Spi::run(set_role).unwrap();

        let copy_to_parquet = "copy test_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY from a file")]
    fn test_with_no_read_files_privilege() {
        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let create_role = "create role test_role;";
        Spi::run(create_role).unwrap();

        let grant_role = "grant ALL ON ALL TABLES IN SCHEMA public TO test_role;";
        Spi::run(grant_role).unwrap();

        let set_role = "set role test_role;";
        Spi::run(set_role).unwrap();

        let copy_from_parquet = "copy test_table from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied for table test_table")]
    fn test_with_no_table_privilege() {
        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let create_role = "create role test_role;";
        Spi::run(create_role).unwrap();

        let grant_role = "grant pg_write_server_files TO test_role;";
        Spi::run(grant_role).unwrap();

        let set_role = "set role test_role;";
        Spi::run(set_role).unwrap();

        let copy_from_parquet = "copy test_table to '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "cannot copy from partitioned table \"partitioned_table\"")]
    fn test_copy_to_partitioned_table() {
        let create_table = "create table partitioned_table(id int) partition by range (id);";
        Spi::run(create_table).unwrap();

        let create_partition =
            "create table partitioned_table_1 partition of partitioned_table for values from (1) to (11);";
        Spi::run(create_partition).unwrap();

        let insert_data = "insert into partitioned_table select i from generate_series(1, 10) i;";
        Spi::run(insert_data).unwrap();

        // success via query form
        let copy_to_parquet = "copy (select * from partitioned_table) to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let total_rows = Spi::get_one::<i64>("select count(*) from partitioned_table;")
            .unwrap()
            .unwrap();
        assert_eq!(total_rows, 10);

        // should fail with partitioned table
        let copy_to_parquet = "copy partitioned_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "cannot copy from view \"test_view\"")]
    fn test_copy_to_view() {
        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let create_view = "create view test_view as select * from test_table;";
        Spi::run(create_view).unwrap();

        let insert_data = "insert into test_table select i from generate_series(1, 10) i;";
        Spi::run(insert_data).unwrap();

        // success via query form
        let copy_to_parquet = "copy (select * from test_view) to '/tmp/test.parquet'";
        Spi::run(copy_to_parquet).unwrap();

        let total_rows = Spi::get_one::<i64>("select count(*) from test_view;")
            .unwrap()
            .unwrap();
        assert_eq!(total_rows, 10);

        // should fail with view
        let copy_to_parquet = "copy test_view to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();
    }

    #[pg_test]
    fn test_copy_to_with_row_level_security() {
        let create_table = "create table test_table(username text);";
        Spi::run(create_table).unwrap();

        let insert_data = "insert into test_table values ('postgres'), ('dummy'), ('test_role');";
        Spi::run(insert_data).unwrap();

        let create_role = "create role test_role;";
        Spi::run(create_role).unwrap();

        let grant_server_file_write = "grant pg_write_server_files to test_role;";
        Spi::run(grant_server_file_write).unwrap();

        let grant_table = "grant all on test_table to test_role;";
        Spi::run(grant_table).unwrap();

        let create_username_policy =
            "create policy test_policy on test_table using (username = current_user);";
        Spi::run(create_username_policy).unwrap();

        let enable_rls = "alter table test_table enable row level security;";
        Spi::run(enable_rls).unwrap();

        let set_role = "set role test_role;";
        Spi::run(set_role).unwrap();

        let copy_to_parquet = "copy test_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let reset_role = "reset role;";
        Spi::run(reset_role).unwrap();

        let truncate_table = "truncate test_table;";
        Spi::run(truncate_table).unwrap();

        let copy_from_parquet = "copy test_table from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();

        let total_rows = Spi::get_one::<i64>("select count(*) from test_table;")
            .unwrap()
            .unwrap();

        assert_eq!(total_rows, 1);
    }

    #[pg_test]
    #[should_panic(expected = "COPY FROM not supported with row-level security")]
    fn test_copy_from_with_row_level_security() {
        let create_table = "create table test_table(username text);";
        Spi::run(create_table).unwrap();

        let insert_data = "insert into test_table values ('postgres'), ('dummy'), ('test_role');";
        Spi::run(insert_data).unwrap();

        let copy_to_parquet = "copy test_table to '/tmp/test.parquet';";
        Spi::run(copy_to_parquet).unwrap();

        let create_role = "create role test_role;";
        Spi::run(create_role).unwrap();

        let grant_server_file_write = "grant pg_read_server_files to test_role;";
        Spi::run(grant_server_file_write).unwrap();

        let grant_table = "grant all on test_table to test_role;";
        Spi::run(grant_table).unwrap();

        let create_username_policy =
            "create policy test_policy on test_table using (username = current_user);";
        Spi::run(create_username_policy).unwrap();

        let enable_rls = "alter table test_table enable row level security;";
        Spi::run(enable_rls).unwrap();

        let set_role = "set role test_role;";
        Spi::run(set_role).unwrap();

        let copy_from_parquet = "copy test_table from '/tmp/test.parquet';";
        Spi::run(copy_from_parquet).unwrap();
    }
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec!["shared_preload_libraries = 'pg_parquet'"]
    }
}
