#[pgrx::pg_schema]
mod tests {
    use std::{cmp::Ordering, collections::HashMap, path::Path};

    use pgrx::{pg_test, Spi};

    use crate::{
        pgrx_tests::common::{
            create_crunchy_map_type, extension_exists, CopyOptionValue, FileCleanup, TestTable,
            LOCAL_TEST_FILE_PATH,
        },
        PgParquetCompression,
    };

    #[pg_test]
    fn test_copy_with_empty_options() {
        let test_table = TestTable::<i32>::new("int4".into())
            .with_copy_to_options(HashMap::new())
            .with_copy_from_options(HashMap::new());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
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
            test_table.assert_expected_and_result_rows();

            let parquet_metadata_command = format!(
                "select compression from parquet.metadata('{}');",
                LOCAL_TEST_FILE_PATH
            );

            let result_compression = Spi::get_one::<String>(&parquet_metadata_command)
                .unwrap()
                .unwrap();

            assert_eq!(expected_compression, result_compression);
        }
    }

    #[pg_test]
    fn test_compression_from_uri() {
        let parquet_uris = vec![
            format!("{}", LOCAL_TEST_FILE_PATH),
            format!("{}.snappy", LOCAL_TEST_FILE_PATH),
            format!("{}.gz", LOCAL_TEST_FILE_PATH),
            format!("{}.br", LOCAL_TEST_FILE_PATH),
            format!("{}.lz4", LOCAL_TEST_FILE_PATH),
            format!("{}.zst", LOCAL_TEST_FILE_PATH),
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
            test_table.assert_expected_and_result_rows();

            let parquet_metadata_command =
                format!("select compression from parquet.metadata('{}');", uri);

            let result_compression = Spi::get_one::<String>(&parquet_metadata_command)
                .unwrap()
                .unwrap();

            assert_eq!(expected_compression, result_compression);
        }
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
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
            .with_uri(format!("{}.snappy", LOCAL_TEST_FILE_PATH));
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "valid compression range 0..=9 exceeded")]
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();

        let parquet_metadata_command = format!(
            "select compression from parquet.metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );

        let result_compression = Spi::get_one::<String>(&parquet_metadata_command)
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
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "Minimum allowed size is 1MB. Got 102400 bytes.")]
    fn test_invalid_file_size_bytes() {
        let _file_cleanup = FileCleanup::new(LOCAL_TEST_FILE_PATH);

        let mut copy_options = HashMap::new();
        copy_options.insert(
            "file_size_bytes".to_string(),
            CopyOptionValue::StringOption("100KB".into()),
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_copy_to_options(copy_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
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
        test_table.assert_expected_and_result_rows();

        let parquet_file_metadata_command = format!(
            "select * from parquet.file_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_file_metadata_command, None, &[])
                .unwrap();

            for row in tup_table {
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                results.push(num_row_groups);
            }

            results
        });

        assert_eq!(result_metadata, vec![1]);
    }

    #[pg_test]
    fn test_row_group_size() {
        let total_rows = 10;
        let row_group_size = 2;
        let total_row_groups = total_rows / row_group_size;

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        let copy_to_parquet = format!(
            "copy (select i as id from generate_series(1,{}) i) to '{}' with (row_group_size {});",
            total_rows, LOCAL_TEST_FILE_PATH, row_group_size
        );
        Spi::run(&copy_to_parquet).unwrap();

        let parquet_file_metadata_command = format!(
            "select * from parquet.file_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_file_metadata_command, None, &[])
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
        let create_table = "create table test_table(id int, name text);";
        Spi::run(create_table).unwrap();

        let insert_data =
            "insert into test_table select i, 'a' from generate_series(1, 1000000) i;";
        Spi::run(insert_data).unwrap();

        let id_bytes = 4;
        let name_bytes = 1;
        let total_rows_size_bytes = (id_bytes + name_bytes) * 1024 * 1024;

        let row_group_size_bytes = total_rows_size_bytes / 10;

        let copy_to_parquet = format!(
            "copy test_table to '{}' with (row_group_size_bytes {});",
            LOCAL_TEST_FILE_PATH, row_group_size_bytes
        );
        Spi::run(&copy_to_parquet).unwrap();

        let parquet_file_metadata_command = format!(
            "select * from parquet.file_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );
        let result_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_file_metadata_command, None, &[])
                .unwrap();

            for row in tup_table {
                let num_row_groups = row["num_row_groups"].value::<i64>().unwrap().unwrap();
                results.push(num_row_groups);
            }

            results
        });

        assert_eq!(result_metadata, vec![12]);
    }

    #[pg_test]
    fn test_file_size_bytes() {
        let uris = [
            // with ".parquet" extension
            LOCAL_TEST_FILE_PATH.to_string(),
            // with ".parquet.gz" extension
            format!("{LOCAL_TEST_FILE_PATH}.gz"),
            // without extension
            LOCAL_TEST_FILE_PATH
                .strip_suffix(".parquet")
                .unwrap()
                .to_string(),
        ];

        let expected_file_counts = [5_usize, 3, 5];

        for (uri, expected_file_count) in uris.into_iter().zip(expected_file_counts) {
            // make sure remove files
            let _file_cleanup = FileCleanup::new(&uri);

            // drop tables
            Spi::run("drop table if exists test_expected, test_result;").unwrap();

            let setup_commands = format!(
                "create table test_expected(a text);\n\
                 create table test_result(a text);\n\
                 insert into test_expected select 'hellooooo' || i from generate_series(1, 1000000) i;\n\
                 copy test_expected to '{uri}' with (format parquet, file_size_bytes '1MB')");
            Spi::run(&setup_commands).unwrap();

            let parent_folder = Path::new(&uri);

            // assert file count
            let mut file_entries = parent_folder
                .read_dir()
                .unwrap()
                .map(|entry_res| entry_res.unwrap())
                .collect::<Vec<_>>();

            assert_eq!(file_entries.len(), expected_file_count);

            file_entries.sort_by(|a, b| {
                if a.file_name() <= b.file_name() {
                    Ordering::Less
                } else {
                    Ordering::Greater
                }
            });

            // assert file paths
            for (file_idx, file_entry) in file_entries.iter().enumerate() {
                let expected_path = parent_folder.join(format!("data_{file_idx}.parquet"));

                let expected_path = expected_path.to_str().unwrap();

                assert_eq!(file_entry.path().to_str().unwrap(), expected_path);

                let copy_from_command =
                    format!("copy test_result from '{expected_path}' with (format parquet)");
                Spi::run(copy_from_command.as_str()).unwrap();
            }

            // assert rows
            let (expected_rows, result_rows) = Spi::connect(|client| {
                let mut expected_rows = Vec::new();
                let tup_table = client
                    .select("select a from test_expected order by 1", None, &[])
                    .unwrap();

                for row in tup_table {
                    let a = row["a"].value::<&str>().unwrap().unwrap();
                    expected_rows.push(a);
                }

                let mut result_rows = Vec::new();
                let tup_table = client
                    .select("select a from test_result order by 1", None, &[])
                    .unwrap();

                for row in tup_table {
                    let a = row["a"].value::<&str>().unwrap().unwrap();
                    result_rows.push(a);
                }

                (expected_rows, result_rows)
            });

            assert_eq!(result_rows, expected_rows);
        }
    }

    #[pg_test]
    #[should_panic(expected = "unrecognized match_by method: invalid_match_by")]
    fn test_invalid_match_by() {
        let mut copy_from_options = HashMap::new();
        copy_from_options.insert(
            "match_by".to_string(),
            CopyOptionValue::StringOption("invalid_match_by".to_string()),
        );

        let test_table =
            TestTable::<i32>::new("int4".into()).with_copy_from_options(copy_from_options);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "invalid JSON string for field_ids")]
    fn test_invalid_field_ids() {
        let setup_commands = "create type person as (id int, a int, b text);
                              create table test_table(a int, b text, c int[], d person);";
        Spi::run(setup_commands).unwrap();

        let copy_to_parquet = format!(
            "copy test_table to '{}' with (field_ids 'invalid_field_ids');",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "duplicate field id 14 in \"field_ids\"")]
    fn test_duplicate_id_in_field_ids() {
        let setup_commands = "create table test_table(a int, b text);";
        Spi::run(setup_commands).unwrap();

        let explicit_field_ids = "{\"a\": 14, \"b\": 14}";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();
    }

    #[pg_test]
    fn test_no_field_ids() {
        let setup_commands = "create type person as (id int, a int, b text);
                              create table test_table(a int, b text, c int[], d person);";
        Spi::run(setup_commands).unwrap();

        let copy_to_parquet = format!(
            "copy test_table to '{}' with (field_ids 'none');",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&copy_to_parquet).unwrap();

        let parquet_metadata_command = format!(
            "select count(*) from parquet.schema('{}') where field_id is not null;",
            LOCAL_TEST_FILE_PATH
        );

        let result_count = Spi::get_one::<i64>(&parquet_metadata_command)
            .unwrap()
            .unwrap();
        assert_eq!(result_count, 0);

        // the default is 'none'
        let copy_to_parquet = format!("copy test_table to '{}';", LOCAL_TEST_FILE_PATH);
        Spi::run(&copy_to_parquet).unwrap();

        let result_count = Spi::get_one::<i64>(&parquet_metadata_command)
            .unwrap()
            .unwrap();
        assert_eq!(result_count, 0);
    }

    #[pg_test]
    fn test_auto_field_ids() {
        let setup_commands = "create type dog as (id int, name text);
                              create type person as (id int, dog dog, dogs dog[]);
                              create table test_table(a int, b text, c person, d person[]);";
        Spi::run(setup_commands).unwrap();

        let copy_to_parquet = format!(
            "copy test_table to '{}' with (field_ids 'auto');",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&copy_to_parquet).unwrap();

        let fields = Spi::connect(|client| {
            let parquet_schema_command = format!(
                "select field_id, name from parquet.schema('{}') order by 1,2;",
                LOCAL_TEST_FILE_PATH
            );

            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let field_id = row["field_id"].value::<i64>().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();

                results.push((field_id, name));
            }

            results
        });

        assert_eq!(
            fields,
            vec![
                (Some(0), "a".into()),
                (Some(1), "b".into()),
                (Some(2), "c".into()),
                (Some(3), "id".into()),
                (Some(4), "dog".into()),
                (Some(5), "id".into()),
                (Some(6), "name".into()),
                (Some(7), "dogs".into()),
                (Some(8), "element".into()),
                (Some(9), "id".into()),
                (Some(10), "name".into()),
                (Some(11), "d".into()),
                (Some(12), "element".into()),
                (Some(13), "id".into()),
                (Some(14), "dog".into()),
                (Some(15), "id".into()),
                (Some(16), "name".into()),
                (Some(17), "dogs".into()),
                (Some(18), "element".into()),
                (Some(19), "id".into()),
                (Some(20), "name".into()),
                (None, "arrow_schema".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into())
            ]
        );
    }

    #[pg_test]
    fn test_explicit_field_ids() {
        let setup_commands = "create type dog as (id int, name text);
                              create type person as (id int, dog dog, dogs dog[]);
                              create table test_table(a int, b text, c person, d person[]);";
        Spi::run(setup_commands).unwrap();

        let explicit_field_ids = "{\"a\": 10,
                                   \"b\": 12,
                                   \"c\": {
                                            \"__root_field_id\": 100,
                                            \"id\": 200,
                                            \"dog\": {
                                                        \"__root_field_id\": 300,
                                                        \"id\": 400,
                                                        \"name\": 500
                                                     },
                                            \"dogs\": {
                                                        \"__root_field_id\": 600,
                                                        \"element\": {
                                                                        \"__root_field_id\": 700,
                                                                        \"id\": 800,
                                                                        \"name\": 900
                                                                     }
                                                      }
                                          },
                                   \"d\": {
                                            \"__root_field_id\": 1000,
                                            \"element\": {
                                                            \"__root_field_id\": 1100,
                                                            \"id\": 1200,
                                                            \"dog\": {
                                                                        \"__root_field_id\": 1300,
                                                                        \"id\": 1400,
                                                                        \"name\": 1500
                                                                     },
                                                            \"dogs\": {
                                                                        \"__root_field_id\": 1600,
                                                                        \"element\": {
                                                                                        \"__root_field_id\": 1700,
                                                                                        \"id\": 1800,
                                                                                        \"name\": 1900
                                                                                     }
                                                                      }
                                                         }
                                          }
                                  }";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();

        let fields = Spi::connect(|client| {
            let parquet_schema_command = format!(
                "select field_id, name from parquet.schema('{}') order by 1,2;",
                LOCAL_TEST_FILE_PATH
            );

            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let field_id = row["field_id"].value::<i64>().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();

                results.push((field_id, name));
            }

            results
        });

        assert_eq!(
            fields,
            vec![
                (Some(10), "a".into()),
                (Some(12), "b".into()),
                (Some(100), "c".into()),
                (Some(200), "id".into()),
                (Some(300), "dog".into()),
                (Some(400), "id".into()),
                (Some(500), "name".into()),
                (Some(600), "dogs".into()),
                (Some(700), "element".into()),
                (Some(800), "id".into()),
                (Some(900), "name".into()),
                (Some(1000), "d".into()),
                (Some(1100), "element".into()),
                (Some(1200), "id".into()),
                (Some(1300), "dog".into()),
                (Some(1400), "id".into()),
                (Some(1500), "name".into()),
                (Some(1600), "dogs".into()),
                (Some(1700), "element".into()),
                (Some(1800), "id".into()),
                (Some(1900), "name".into()),
                (None, "arrow_schema".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into())
            ]
        );
    }

    #[pg_test]
    fn test_auto_field_ids_with_map() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            return;
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        let int_text_map_type = create_crunchy_map_type("int", "text");
        let setup_commands = format!("create type dog as (id int, name text);
                                      create type person as (id int, dog dog, dogs dog[], names {int_text_map_type});
                                      create type address as (street text, city text);"
                                    );
        Spi::run(&setup_commands).unwrap();

        let int_adresses_map_type = create_crunchy_map_type("int", "address[]");
        let create_table = format!(
            "create table test_table(a int, b text, c person, d person[], addresses {int_adresses_map_type});"
        );
        Spi::run(&create_table).unwrap();

        let copy_to_parquet =
            format!("copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids 'auto');");
        Spi::run(&copy_to_parquet).unwrap();

        let fields = Spi::connect(|client| {
            let parquet_schema_command = format!(
                "select field_id, name from parquet.schema('{}') order by 1,2;",
                LOCAL_TEST_FILE_PATH
            );

            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let field_id = row["field_id"].value::<i64>().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();

                results.push((field_id, name));
            }

            results
        });

        assert_eq!(
            fields,
            vec![
                (Some(0), "a".into()),
                (Some(1), "b".into()),
                (Some(2), "c".into()),
                (Some(3), "id".into()),
                (Some(4), "dog".into()),
                (Some(5), "id".into()),
                (Some(6), "name".into()),
                (Some(7), "dogs".into()),
                (Some(8), "element".into()),
                (Some(9), "id".into()),
                (Some(10), "name".into()),
                (Some(11), "names".into()),
                (Some(12), "key".into()),
                (Some(13), "val".into()),
                (Some(14), "d".into()),
                (Some(15), "element".into()),
                (Some(16), "id".into()),
                (Some(17), "dog".into()),
                (Some(18), "id".into()),
                (Some(19), "name".into()),
                (Some(20), "dogs".into()),
                (Some(21), "element".into()),
                (Some(22), "id".into()),
                (Some(23), "name".into()),
                (Some(24), "names".into()),
                (Some(25), "key".into()),
                (Some(26), "val".into()),
                (Some(27), "addresses".into()),
                (Some(28), "key".into()),
                (Some(29), "val".into()),
                (Some(30), "element".into()),
                (Some(31), "street".into()),
                (Some(32), "city".into()),
                (None, "arrow_schema".into()),
                (None, "key_value".into()),
                (None, "key_value".into()),
                (None, "key_value".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into())
            ]
        );
    }

    #[pg_test]
    fn test_explicit_field_ids_with_map() {
        // Skip the test if crunchy_map extension is not available
        if !extension_exists("crunchy_map") {
            return;
        }

        Spi::run("DROP EXTENSION IF EXISTS crunchy_map; CREATE EXTENSION crunchy_map;").unwrap();

        let int_text_map_type = create_crunchy_map_type("int", "text");
        let setup_commands = format!("create type dog as (id int, name text);
                                      create type person as (id int, dog dog, dogs dog[], names {int_text_map_type});
                                      create type address as (street text, city text);"
                                    );
        Spi::run(&setup_commands).unwrap();

        let int_adresses_map_type = create_crunchy_map_type("int", "address[]");
        let create_table = format!(
            "create table test_table(a int, b text, c person, d person[], addresses {int_adresses_map_type});"
        );
        Spi::run(&create_table).unwrap();

        let explicit_field_ids = "{\"a\": 10,
                                   \"b\": 12,
                                   \"c\": {
                                            \"__root_field_id\": 100,
                                            \"id\": 200,
                                            \"dog\": {
                                                        \"__root_field_id\": 300,
                                                        \"id\": 400,
                                                        \"name\": 500
                                                     },
                                            \"dogs\": {
                                                        \"__root_field_id\": 600,
                                                        \"element\": {
                                                                        \"__root_field_id\": 700,
                                                                        \"id\": 800,
                                                                        \"name\": 900
                                                                     }
                                                      },
                                            \"names\": {
                                                        \"__root_field_id\": 1000,
                                                        \"key\": 1100,
                                                        \"val\": 1200
                                                      }
                                          },
                                   \"d\": {
                                            \"__root_field_id\": 1300,
                                            \"element\": {
                                                            \"__root_field_id\": 1400,
                                                            \"id\": 1500,
                                                            \"dog\": {
                                                                        \"__root_field_id\": 1600,
                                                                        \"id\": 1700,
                                                                        \"name\": 1800
                                                                     },
                                                            \"dogs\": {
                                                                        \"__root_field_id\": 1900,
                                                                        \"element\": {
                                                                                        \"__root_field_id\": 2000,
                                                                                        \"id\": 2100,
                                                                                        \"name\": 2200
                                                                                     }
                                                                      },
                                                            \"names\": {
                                                                        \"__root_field_id\": 2300,
                                                                        \"key\": 2400,
                                                                        \"val\": 2500
                                                                      }
                                                         }
                                          },
                                   \"addresses\": {
                                                    \"__root_field_id\": 2600,
                                                    \"key\": 2700,
                                                    \"val\": {
                                                                \"__root_field_id\": 2800,
                                                                \"element\": {
                                                                                \"__root_field_id\": 2900,
                                                                                \"street\": 3000,
                                                                                \"city\": 3100
                                                                             }
                                                              }
                                                  }
                                  }";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();

        let fields = Spi::connect(|client| {
            let parquet_schema_command = format!(
                "select field_id, name from parquet.schema('{}') order by 1,2;",
                LOCAL_TEST_FILE_PATH
            );

            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let field_id = row["field_id"].value::<i64>().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();

                results.push((field_id, name));
            }

            results
        });

        assert_eq!(
            fields,
            vec![
                (Some(10), "a".into()),
                (Some(12), "b".into()),
                (Some(100), "c".into()),
                (Some(200), "id".into()),
                (Some(300), "dog".into()),
                (Some(400), "id".into()),
                (Some(500), "name".into()),
                (Some(600), "dogs".into()),
                (Some(700), "element".into()),
                (Some(800), "id".into()),
                (Some(900), "name".into()),
                (Some(1000), "names".into()),
                (Some(1100), "key".into()),
                (Some(1200), "val".into()),
                (Some(1300), "d".into()),
                (Some(1400), "element".into()),
                (Some(1500), "id".into()),
                (Some(1600), "dog".into()),
                (Some(1700), "id".into()),
                (Some(1800), "name".into()),
                (Some(1900), "dogs".into()),
                (Some(2000), "element".into()),
                (Some(2100), "id".into()),
                (Some(2200), "name".into()),
                (Some(2300), "names".into()),
                (Some(2400), "key".into()),
                (Some(2500), "val".into()),
                (Some(2600), "addresses".into()),
                (Some(2700), "key".into()),
                (Some(2800), "val".into()),
                (Some(2900), "element".into()),
                (Some(3000), "street".into()),
                (Some(3100), "city".into()),
                (None, "arrow_schema".into()),
                (None, "key_value".into()),
                (None, "key_value".into()),
                (None, "key_value".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into())
            ]
        );
    }

    #[pg_test]
    fn test_explicit_field_ids_with_missing_field_ids() {
        let setup_commands = "create type dog as (id int, name text);
                              create type person as (id int, dog dog, dogs dog[]);
                              create table test_table(a int, b text, c person, d person[]);";
        Spi::run(setup_commands).unwrap();

        let explicit_field_ids = "{\"a\": 10,
                                   \"c\": {
                                            \"__root_field_id\": 100,
                                            \"id\": 200,
                                            \"dog\": {
                                                        \"id\": 400,
                                                        \"name\": 500
                                                     },
                                            \"dogs\": {
                                                        \"__root_field_id\": 600,
                                                        \"element\": {
                                                                        \"__root_field_id\": 700,
                                                                        \"id\": 800,
                                                                        \"name\": 900
                                                                     }
                                                      }
                                          },
                                   \"d\": {
                                            \"__root_field_id\": 1000,
                                            \"element\": {
                                                            \"dog\": {
                                                                        \"__root_field_id\": 1300,
                                                                        \"id\": 1400,
                                                                        \"name\": 1500
                                                                     },
                                                            \"dogs\": {
                                                                        \"__root_field_id\": 1600,
                                                                        \"element\": {
                                                                                        \"__root_field_id\": 1700,
                                                                                        \"id\": 1800
                                                                                     }
                                                                      }
                                                         }
                                          }
                                  }";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();

        let fields = Spi::connect(|client| {
            let parquet_schema_command = format!(
                "select field_id, name from parquet.schema('{}') order by 1,2;",
                LOCAL_TEST_FILE_PATH
            );

            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();
            let mut results = Vec::new();

            for row in tup_table {
                let field_id = row["field_id"].value::<i64>().unwrap();
                let name = row["name"].value::<String>().unwrap().unwrap();

                results.push((field_id, name));
            }

            results
        });

        assert_eq!(
            fields,
            vec![
                (Some(10), "a".into()),
                (Some(100), "c".into()),
                (Some(200), "id".into()),
                (Some(400), "id".into()),
                (Some(500), "name".into()),
                (Some(600), "dogs".into()),
                (Some(700), "element".into()),
                (Some(800), "id".into()),
                (Some(900), "name".into()),
                (Some(1000), "d".into()),
                (Some(1300), "dog".into()),
                (Some(1400), "id".into()),
                (Some(1500), "name".into()),
                (Some(1600), "dogs".into()),
                (Some(1700), "element".into()),
                (Some(1800), "id".into()),
                (None, "arrow_schema".into()),
                (None, "b".into()),
                (None, "dog".into()),
                (None, "element".into()),
                (None, "id".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "list".into()),
                (None, "name".into()),
            ]
        );
    }

    #[pg_test]
    #[should_panic(expected = "Available fields: [\"a\", \"b\", \"c\", \"d\"]")]
    fn test_explicit_field_ids_invalid_json() {
        let setup_commands = "create type dog as (id int, name text);
                              create type person as (id int, dog dog, dogs dog[]);
                              create table test_table(a int, b text, c person, d person[]);";
        Spi::run(setup_commands).unwrap();

        let explicit_field_ids = "{\"aa\": 10, \"b\": 12}";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "Available fields: [\"id\", \"name\"]")]
    fn test_explicit_field_ids_another_invalid_json() {
        let setup_commands = "create type dog as (id int, name text);
                              create type person as (id int, dog dog, dogs dog[]);
                              create table test_table(a int, b text, c person, d person[]);";
        Spi::run(setup_commands).unwrap();

        let explicit_field_ids = "{\"a\": 10,
                                   \"d\": {
                                            \"__root_field_id\": 1000,
                                            \"element\": {
                                                            \"__root_field_id\": 1100,
                                                            \"id\": 1200,
                                                            \"dog\": {
                                                                        \"__root_field_id\": 1300,
                                                                        \"id\": 1400,
                                                                        \"name\": 1500
                                                                     },
                                                            \"dogs\": {
                                                                        \"__root_field_id\": 1600,
                                                                        \"element\": {
                                                                                        \"__root_field_id\": 1700,
                                                                                        \"iddd\": 1800,
                                                                                        \"name\": 1900
                                                                                     }
                                                                      }
                                                         }
                                          }
                                  }";

        let copy_to_parquet = format!(
            "copy test_table to '{LOCAL_TEST_FILE_PATH}' with (field_ids '{explicit_field_ids}');"
        );
        Spi::run(&copy_to_parquet).unwrap();
    }
}
