#[pgrx::pg_schema]
mod tests {
    use pgrx::{pg_test, Spi};

    use crate::pgrx_tests::common::LOCAL_TEST_FILE_PATH;

    #[pg_test]
    fn test_parquet_schema() {
        let ddls = format!(
            "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            copy workers to '{}';
        ",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&ddls).unwrap();

        let parquet_schema_command = format!(
            "select * from parquet.schema('{}') ORDER BY name, converted_type;",
            LOCAL_TEST_FILE_PATH
        );

        let result_schema = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client.select(&parquet_schema_command, None, &[]).unwrap();

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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
                "element".into(),
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
            (
                LOCAL_TEST_FILE_PATH.into(),
                "element".into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
            copy workers to '{}' with (row_group_size {});
        ",
            total_rows, LOCAL_TEST_FILE_PATH, row_group_size
        );
        Spi::run(&ddls).unwrap();

        let parquet_metadata_command = format!(
            "select * from parquet.metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );

        // Debug (assert_eq! requires) is only implemented for tuples up to 12 elements. This is why we split the
        // metadata into two parts.
        let (result_metadata_part1, result_metadata_part2) = Spi::connect(|client| {
            let mut results_part1 = Vec::new();
            let mut results_part2 = Vec::new();

            let tup_table = client.select(&parquet_metadata_command, None, &[]).unwrap();

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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
                0,
                5,
                5,
                250,
                1,
                0,
                5,
                "workers.list.element.p.list.element.id".into(),
                "INT32".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
                0,
                5,
                5,
                250,
                2,
                0,
                5,
                "workers.list.element.p.list.element.name".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
                0,
                5,
                5,
                250,
                3,
                0,
                5,
                "workers.list.element.monthly_salary".into(),
                "INT64".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
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
                LOCAL_TEST_FILE_PATH.into(),
                1,
                5,
                5,
                250,
                1,
                0,
                5,
                "workers.list.element.p.list.element.id".into(),
                "INT32".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
                1,
                5,
                5,
                250,
                2,
                0,
                5,
                "workers.list.element.p.list.element.name".into(),
                "BYTE_ARRAY".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
                1,
                5,
                5,
                250,
                3,
                0,
                5,
                "workers.list.element.monthly_salary".into(),
                "INT64".into(),
                Some(5),
                None,
            ),
            (
                LOCAL_TEST_FILE_PATH.into(),
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
            copy workers to '{}' with (row_group_size {});
        ",
            total_rows, LOCAL_TEST_FILE_PATH, row_group_size
        );
        Spi::run(&ddls).unwrap();

        let parquet_file_metadata_command = format!(
            "select * from parquet.file_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );
        let result_file_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_file_metadata_command, None, &[])
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
            LOCAL_TEST_FILE_PATH.into(),
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
        let ddls = format!(
            "
            create type person AS (id int, name text);
            create type worker AS (p person[], monthly_salary decimal(15,6));
            create table workers (id int, workers worker[], company text);
            copy workers to '{}';
        ",
            LOCAL_TEST_FILE_PATH
        );
        Spi::run(&ddls).unwrap();

        let parquet_kv_metadata_command = format!(
            "select * from parquet.kv_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );

        let result_kv_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_kv_metadata_command, None, &[])
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
            LOCAL_TEST_FILE_PATH.into(),
            vec![65, 82, 82, 79, 87, 58, 115, 99, 104, 101, 109, 97],
            Some(vec![
                47, 47, 47, 47, 47, 43, 119, 68, 65, 65, 65, 81, 65, 65, 65, 65, 65, 65, 65, 75,
                65, 65, 119, 65, 67, 103, 65, 74, 65, 65, 81, 65, 67, 103, 65, 65, 65, 66, 65, 65,
                65, 65, 65, 65, 65, 81, 81, 65, 67, 65, 65, 73, 65, 65, 65, 65, 66, 65, 65, 73, 65,
                65, 65, 65, 66, 65, 65, 65, 65, 65, 77, 65, 65, 65, 66, 73, 65, 119, 65, 65, 97,
                65, 65, 65, 65, 65, 81, 65, 65, 65, 68, 87, 47, 80, 47, 47, 75, 65, 65, 65, 65, 66,
                81, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 66, 81, 119, 65, 65, 65,
                65, 65, 65, 65, 65, 65, 109, 80, 55, 47, 47, 119, 99, 65, 65, 65, 66, 106, 98, 50,
                49, 119, 89, 87, 53, 53, 65, 65, 69, 65, 65, 65, 65, 69, 65, 65, 65, 65, 117, 80,
                122, 47, 47, 119, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 81, 65, 65, 65, 68,
                103, 65, 65, 65, 65, 81, 65, 65, 65, 65, 85, 69, 70, 83, 85, 86, 86, 70, 86, 68,
                112, 109, 97, 87, 86, 115, 90, 70, 57, 112, 90, 65, 65, 65, 65, 65, 65, 50, 47,
                102, 47, 47, 108, 65, 73, 65, 65, 66, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65,
                65, 65, 66, 68, 72, 103, 67, 65, 65, 65, 66, 65, 65, 65, 65, 67, 65, 65, 65, 65,
                80, 122, 43, 47, 47, 57, 97, 47, 102, 47, 47, 77, 65, 73, 65, 65, 66, 119, 65, 65,
                65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 68, 82, 81, 67, 65, 65, 65, 67, 65, 65,
                65, 65, 102, 65, 65, 65, 65, 65, 103, 65, 65, 65, 65, 107, 47, 47, 47, 47, 103,
                118, 51, 47, 47, 122, 103, 65, 65, 65, 65, 85, 65, 65, 65, 65, 68, 65, 65, 65, 65,
                65, 65, 65, 65, 81, 99, 85, 65, 65, 65, 65, 65, 65, 65, 65, 65, 69, 122, 57, 47,
                47, 56, 71, 65, 65, 65, 65, 68, 119, 65, 65, 65, 65, 52, 65, 65, 65, 66, 116, 98,
                50, 53, 48, 97, 71, 120, 53, 88, 51, 78, 104, 98, 71, 70, 121, 101, 81, 65, 65, 65,
                81, 65, 65, 65, 65, 81, 65, 65, 65, 66, 48, 47, 102, 47, 47, 67, 65, 65, 65, 65,
                65, 119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 78, 119, 65, 65, 65, 66, 65, 65, 65,
                65, 66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109, 90, 112, 90, 87, 120, 107, 88,
                50, 108, 107, 65, 65, 65, 65, 65, 80, 76, 57, 47, 47, 57, 89, 65, 81, 65, 65, 71,
                65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 65, 65, 65, 69, 77, 81, 65, 69, 65, 65,
                65, 69, 65, 65, 65, 65, 73, 65, 65, 65, 65, 117, 80, 47, 47, 47, 120, 98, 43, 47,
                47, 47, 52, 65, 65, 65, 65, 72, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 65, 65,
                65, 69, 78, 51, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66, 119, 65, 65, 65, 65, 67,
                65, 65, 65, 65, 79, 68, 47, 47, 47, 56, 43, 47, 118, 47, 47, 76, 65, 65, 65, 65,
                66, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 66, 82, 65, 65, 65,
                65, 65, 65, 65, 65, 65, 65, 66, 65, 65, 69, 65, 65, 81, 65, 65, 65, 65, 69, 65, 65,
                65, 65, 98, 109, 70, 116, 90, 81, 65, 65, 65, 65, 65, 66, 65, 65, 65, 65, 66, 65,
                65, 65, 65, 67, 84, 43, 47, 47, 56, 73, 65, 65, 65, 65, 68, 65, 65, 65, 65, 65, 69,
                65, 65, 65, 65, 50, 65, 65, 65, 65, 69, 65, 65, 65, 65, 70, 66, 66, 85, 108, 70,
                86, 82, 86, 81, 54, 90, 109, 108, 108, 98, 71, 82, 102, 97, 87, 81, 65, 65, 65, 65,
                65, 111, 118, 55, 47, 47, 121, 119, 65, 65, 65, 65, 81, 65, 65, 65, 65, 71, 65, 65,
                65, 65, 65, 65, 65, 65, 81, 73, 85, 65, 65, 65, 65, 107, 80, 55, 47, 47, 121, 65,
                65, 65, 65, 65, 65, 65, 65, 65, 66, 65, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66,
                112, 90, 65, 65, 65, 65, 81, 65, 65, 65, 65, 81, 65, 65, 65, 67, 73, 47, 118, 47,
                47, 67, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 78, 81, 65,
                65, 65, 66, 65, 65, 65, 65, 66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109, 90,
                112, 90, 87, 120, 107, 88, 50, 108, 107, 65, 65, 65, 65, 65, 65, 99, 65, 65, 65,
                66, 108, 98, 71, 86, 116, 90, 87, 53, 48, 65, 65, 69, 65, 65, 65, 65, 69, 65, 65,
                65, 65, 121, 80, 55, 47, 47, 119, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 81,
                65, 65, 65, 68, 81, 65, 65, 65, 65, 81, 65, 65, 65, 65, 85, 69, 70, 83, 85, 86, 86,
                70, 86, 68, 112, 109, 97, 87, 86, 115, 90, 70, 57, 112, 90, 65, 65, 65, 65, 65, 65,
                66, 65, 65, 65, 65, 99, 65, 65, 65, 65, 65, 69, 65, 65, 65, 65, 69, 65, 65, 65, 65,
                66, 80, 47, 47, 47, 119, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 81, 65, 65,
                65, 68, 77, 65, 65, 65, 65, 81, 65, 65, 65, 65, 85, 69, 70, 83, 85, 86, 86, 70, 86,
                68, 112, 109, 97, 87, 86, 115, 90, 70, 57, 112, 90, 65, 65, 65, 65, 65, 65, 72, 65,
                65, 65, 65, 90, 87, 120, 108, 98, 87, 86, 117, 100, 65, 65, 66, 65, 65, 65, 65, 66,
                65, 65, 65, 65, 69, 84, 47, 47, 47, 56, 73, 65, 65, 65, 65, 68, 65, 65, 65, 65, 65,
                69, 65, 65, 65, 65, 121, 65, 65, 65, 65, 69, 65, 65, 65, 65, 70, 66, 66, 85, 108,
                70, 86, 82, 86, 81, 54, 90, 109, 108, 108, 98, 71, 82, 102, 97, 87, 81, 65, 65, 65,
                65, 65, 66, 119, 65, 65, 65, 72, 100, 118, 99, 109, 116, 108, 99, 110, 77, 65, 65,
                81, 65, 65, 65, 65, 81, 65, 65, 65, 67, 69, 47, 47, 47, 47, 67, 65, 65, 65, 65, 65,
                119, 65, 65, 65, 65, 66, 65, 65, 65, 65, 77, 81, 65, 65, 65, 66, 65, 65, 65, 65,
                66, 81, 81, 86, 74, 82, 86, 85, 86, 85, 79, 109, 90, 112, 90, 87, 120, 107, 88, 50,
                108, 107, 65, 65, 65, 83, 65, 66, 103, 65, 70, 65, 65, 83, 65, 66, 77, 65, 67, 65,
                65, 65, 65, 65, 119, 65, 66, 65, 65, 83, 65, 65, 65, 65, 78, 65, 65, 65, 65, 66,
                103, 65, 65, 65, 65, 103, 65, 65, 65, 65, 65, 65, 65, 66, 65, 104, 119, 65, 65, 65,
                65, 73, 65, 65, 119, 65, 66, 65, 65, 76, 65, 65, 103, 65, 65, 65, 65, 103, 65, 65,
                65, 65, 65, 65, 65, 65, 65, 81, 65, 65, 65, 65, 65, 67, 65, 65, 65, 65, 97, 87, 81,
                65, 65, 65, 69, 65, 65, 65, 65, 77, 65, 65, 65, 65, 67, 65, 65, 77, 65, 65, 103,
                65, 66, 65, 65, 73, 65, 65, 65, 65, 67, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65,
                66, 65, 65, 65, 65, 77, 65, 65, 65, 65, 66, 65, 65, 65, 65, 66, 81, 81, 86, 74, 82,
                86, 85, 86, 85, 79, 109, 90, 112, 90, 87, 120, 107, 88, 50, 108, 107, 65, 65, 65,
                65, 65, 65, 61, 61,
            ]),
        )];

        assert_eq!(result_kv_metadata, expected_kv_metadata);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }
}
