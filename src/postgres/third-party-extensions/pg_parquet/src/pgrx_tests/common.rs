use std::fs::File;
use std::marker::PhantomData;
use std::path::Path;
use std::{collections::HashMap, fmt::Debug};

use crate::type_compat::map::Map;

use arrow::array::RecordBatch;
use arrow_schema::SchemaRef;
use parquet::arrow::ArrowWriter;
use pgrx::{
    datum::{Time, TimeWithTimeZone},
    FromDatum, IntoDatum, Spi,
};
use pgrx::{Json, JsonB};

pub(crate) enum CopyOptionValue {
    StringOption(String),
    IntOption(i64),
}

pub(crate) fn comma_separated_copy_options(options: &HashMap<String, CopyOptionValue>) -> String {
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

pub(crate) const LOCAL_TEST_FILE_PATH: &str = "/tmp/pg_parquet_test.parquet";

pub(crate) struct FileCleanup {
    path: String,
}

impl FileCleanup {
    pub(crate) fn new(path: &str) -> Self {
        let path = Path::new(path);
        std::fs::remove_dir_all(path).ok();
        std::fs::remove_file(path).ok();

        Self {
            path: path.to_str().unwrap().to_string(),
        }
    }
}

impl Drop for FileCleanup {
    fn drop(&mut self) {
        let path = Path::new(&self.path);
        std::fs::remove_dir_all(path).ok();
        std::fs::remove_file(path).ok();
    }
}

pub(crate) struct TestTable<T: IntoDatum + FromDatum> {
    uri: String,
    order_by_col: String,
    copy_to_options: HashMap<String, CopyOptionValue>,
    copy_from_options: HashMap<String, CopyOptionValue>,
    _data: PhantomData<T>,
}

impl<T: IntoDatum + FromDatum> TestTable<T> {
    pub(crate) fn new(typename: String) -> Self {
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

        let uri = LOCAL_TEST_FILE_PATH.to_string();

        let order_by_col = "a".to_string();

        Self {
            uri,
            order_by_col,
            copy_to_options,
            copy_from_options,
            _data: PhantomData,
        }
    }

    pub(crate) fn with_order_by_col(mut self, order_by_col: String) -> Self {
        self.order_by_col = order_by_col;
        self
    }

    pub(crate) fn with_copy_to_options(
        mut self,
        copy_to_options: HashMap<String, CopyOptionValue>,
    ) -> Self {
        self.copy_to_options = copy_to_options;
        self
    }

    pub(crate) fn with_copy_from_options(
        mut self,
        copy_from_options: HashMap<String, CopyOptionValue>,
    ) -> Self {
        self.copy_from_options = copy_from_options;
        self
    }

    pub(crate) fn with_uri(mut self, uri: String) -> Self {
        self.uri = uri;
        self
    }

    pub(crate) fn insert(&self, insert_command: &str) {
        Spi::run(insert_command).unwrap();
    }

    fn select_all(&self, table_name: &str) -> Vec<(Option<T>,)> {
        let select_command = format!(
            "SELECT a FROM {} ORDER BY {};",
            table_name, self.order_by_col
        );

        Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(&select_command, None, &[]).unwrap();

            for row in tup_table {
                let val = row["a"].value::<T>();
                results.push((val.expect("could not select"),));
            }

            results
        })
    }

    pub(crate) fn copy_to_parquet(&self, table_name: &str) {
        let mut copy_to_query = format!("COPY (SELECT a FROM {}) TO '{}'", table_name, self.uri);

        if !self.copy_to_options.is_empty() {
            copy_to_query.push_str(" WITH (");

            let options_str = comma_separated_copy_options(&self.copy_to_options);
            copy_to_query.push_str(&options_str);

            copy_to_query.push(')');
        }

        copy_to_query.push(';');

        Spi::run(copy_to_query.as_str()).unwrap();
    }

    pub(crate) fn copy_from_parquet(&self, table_name: &str) {
        let mut copy_from_query = format!("COPY {} FROM '{}'", table_name, self.uri);

        if !self.copy_from_options.is_empty() {
            copy_from_query.push_str(" WITH (");

            let options_str = comma_separated_copy_options(&self.copy_from_options);
            copy_from_query.push_str(&options_str);

            copy_from_query.push(')');
        }

        copy_from_query.push(';');

        Spi::run(copy_from_query.as_str()).unwrap();
    }

    pub(crate) fn select_expected_and_result_rows(&self) -> TestResult<T> {
        self.copy_to_parquet("test_expected");
        self.copy_from_parquet("test_result");

        let expected = self.select_all("test_expected");
        let result = self.select_all("test_result");

        TestResult { expected, result }
    }

    pub(crate) fn assert_expected_and_result_rows(&self)
    where
        T: Debug + PartialEq,
    {
        let test_result = self.select_expected_and_result_rows();
        test_result.assert();
    }
}

pub(crate) struct TestResult<T> {
    pub(crate) expected: Vec<(Option<T>,)>,
    pub(crate) result: Vec<(Option<T>,)>,
}

impl<T> TestResult<T>
where
    T: Debug + PartialEq,
{
    // almost all types are comparable by common equality
    pub(crate) fn assert(&self) {
        for (expected, actual) in self.expected.iter().zip(self.result.iter()) {
            assert_eq!(expected, actual);
        }
    }
}

pub(crate) fn assert_int_text_map(expected: Option<Map>, actual: Option<Map>) {
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

pub(crate) fn assert_float(expected_result: Vec<Option<f32>>, result: Vec<Option<f32>>) {
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

pub(crate) fn assert_double(expected_result: Vec<Option<f64>>, result: Vec<Option<f64>>) {
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

pub(crate) fn assert_json(expected: Vec<Option<Json>>, result: Vec<Option<Json>>) {
    for (expected, actual) in expected.into_iter().zip(result.into_iter()) {
        if expected.is_none() {
            assert!(actual.is_none());
        }

        if expected.is_some() {
            assert!(actual.is_some());

            let expected = expected.unwrap();
            let actual = actual.unwrap();

            assert_eq!(expected.0, actual.0);
        }
    }
}

pub(crate) fn assert_jsonb(expected: Vec<Option<JsonB>>, result: Vec<Option<JsonB>>) {
    for (expected, actual) in expected.into_iter().zip(result.into_iter()) {
        if expected.is_none() {
            assert!(actual.is_none());
        }

        if expected.is_some() {
            assert!(actual.is_some());

            let expected = expected.unwrap();
            let actual = actual.unwrap();

            assert_eq!(expected.0, actual.0);
        }
    }
}

pub(crate) fn timetz_to_utc_time(timetz: TimeWithTimeZone) -> Option<Time> {
    Some(timetz.to_utc())
}

pub(crate) fn timetz_array_to_utc_time_array(
    timetz_array: Vec<Option<TimeWithTimeZone>>,
) -> Option<Vec<Option<Time>>> {
    Some(
        timetz_array
            .into_iter()
            .map(|timetz| timetz.map(|timetz| timetz.to_utc()))
            .collect(),
    )
}

pub(crate) fn extension_exists(extension_name: &str) -> bool {
    let query = format!(
        "select count(*) = 1 from pg_available_extensions where name = '{}'",
        extension_name
    );

    Spi::get_one(&query).unwrap().unwrap()
}

pub(crate) fn write_record_batch_to_parquet(schema: SchemaRef, record_batch: RecordBatch) {
    let file = File::create(LOCAL_TEST_FILE_PATH).unwrap();
    let mut writer = ArrowWriter::try_new(file, schema, None).unwrap();

    writer.write(&record_batch).unwrap();
    writer.close().unwrap();
}

pub(crate) fn create_crunchy_map_type(key_type: &str, val_type: &str) -> String {
    assert!(extension_exists("crunchy_map"));

    let command = format!("SELECT crunchy_map.create('{key_type}','{val_type}')::text;",);
    Spi::get_one(&command).unwrap().unwrap()
}
