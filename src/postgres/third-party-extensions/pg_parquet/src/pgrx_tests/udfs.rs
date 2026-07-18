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
                None,
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
                None,
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
                None,
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
                None,
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
                None,
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
                None,
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
                None,
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
                None,
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
            "select uri, encode(key, 'escape') as key, value from parquet.kv_metadata('{}');",
            LOCAL_TEST_FILE_PATH
        );

        let result_kv_metadata = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_kv_metadata_command, None, &[])
                .unwrap();

            for row in tup_table {
                let uri = row["uri"].value::<String>().unwrap().unwrap();
                let key = row["key"].value::<String>().unwrap().unwrap();
                let value = row["value"].value::<Vec<u8>>().unwrap();

                results.push((uri, key, value));
            }

            results
        });

        let expected_kv_metadata = vec![(
            LOCAL_TEST_FILE_PATH.into(),
            "ARROW:schema".into(),
            Some(vec![
                47, 47, 47, 47, 47, 47, 81, 66, 65, 65, 65, 81, 65, 65, 65, 65, 65, 65, 65, 75, 65,
                65, 119, 65, 67, 103, 65, 74, 65, 65, 81, 65, 67, 103, 65, 65, 65, 66, 65, 65, 65,
                65, 65, 65, 65, 81, 81, 65, 67, 65, 65, 73, 65, 65, 65, 65, 66, 65, 65, 73, 65, 65,
                65, 65, 66, 65, 65, 65, 65, 65, 77, 65, 65, 65, 67, 81, 65, 81, 65, 65, 77, 65, 65,
                65, 65, 65, 81, 65, 65, 65, 67, 77, 47, 118, 47, 47, 70, 65, 65, 65, 65, 65, 119,
                65, 65, 65, 65, 65, 65, 65, 69, 70, 68, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 81,
                47, 47, 47, 47, 66, 119, 65, 65, 65, 71, 78, 118, 98, 88, 66, 104, 98, 110, 107,
                65, 116, 80, 55, 47, 47, 120, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65,
                66, 68, 68, 65, 66, 65, 65, 65, 66, 65, 65, 65, 65, 67, 65, 65, 65, 65, 68, 122,
                47, 47, 47, 47, 85, 47, 118, 47, 47, 72, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65,
                65, 65, 65, 69, 78, 66, 65, 69, 65, 65, 65, 73, 65, 65, 65, 66, 77, 65, 65, 65, 65,
                67, 65, 65, 65, 65, 71, 68, 47, 47, 47, 47, 52, 47, 118, 47, 47, 72, 65, 65, 65,
                65, 65, 119, 65, 65, 65, 65, 65, 65, 65, 69, 72, 72, 65, 65, 65, 65, 65, 65, 65,
                65, 65, 65, 73, 65, 65, 119, 65, 67, 65, 65, 69, 65, 65, 103, 65, 65, 65, 65, 71,
                65, 65, 65, 65, 68, 119, 65, 65, 65, 65, 52, 65, 65, 65, 66, 116, 98, 50, 53, 48,
                97, 71, 120, 53, 88, 51, 78, 104, 98, 71, 70, 121, 101, 81, 65, 65, 79, 80, 47, 47,
                47, 120, 103, 65, 65, 65, 65, 77, 65, 65, 65, 65, 65, 65, 65, 66, 68, 74, 103, 65,
                65, 65, 65, 66, 65, 65, 65, 65, 67, 65, 65, 65, 65, 77, 68, 47, 47, 47, 57, 89, 47,
                47, 47, 47, 72, 65, 65, 65, 65, 65, 119, 65, 65, 65, 65, 65, 65, 65, 69, 78, 98,
                65, 65, 65, 65, 65, 73, 65, 65, 65, 65, 52, 65, 65, 65, 65, 67, 65, 65, 65, 65, 79,
                84, 47, 47, 47, 57, 56, 47, 47, 47, 47, 71, 65, 65, 65, 65, 65, 119, 65, 65, 65,
                65, 65, 65, 65, 69, 70, 69, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 69, 65, 65, 81,
                65, 66, 65, 65, 65, 65, 65, 81, 65, 65, 65, 66, 117, 89, 87, 49, 108, 65, 65, 65,
                65, 65, 75, 106, 47, 47, 47, 56, 81, 65, 65, 65, 65, 71, 65, 65, 65, 65, 65, 65,
                65, 65, 81, 73, 85, 65, 65, 65, 65, 109, 80, 47, 47, 47, 121, 65, 65, 65, 65, 65,
                65, 65, 65, 65, 66, 65, 65, 65, 65, 65, 65, 73, 65, 65, 65, 66, 112, 90, 65, 65,
                65, 66, 119, 65, 65, 65, 71, 86, 115, 90, 87, 49, 108, 98, 110, 81, 65, 65, 81, 65,
                65, 65, 72, 65, 65, 65, 65, 65, 72, 65, 65, 65, 65, 90, 87, 120, 108, 98, 87, 86,
                117, 100, 65, 65, 72, 65, 65, 65, 65, 100, 50, 57, 121, 97, 50, 86, 121, 99, 119,
                65, 81, 65, 66, 81, 65, 69, 65, 65, 79, 65, 65, 56, 65, 66, 65, 65, 65, 65, 65,
                103, 65, 69, 65, 65, 65, 65, 66, 103, 65, 65, 65, 65, 103, 65, 65, 65, 65, 65, 65,
                65, 66, 65, 104, 119, 65, 65, 65, 65, 73, 65, 65, 119, 65, 66, 65, 65, 76, 65, 65,
                103, 65, 65, 65, 65, 103, 65, 65, 65, 65, 65, 65, 65, 65, 65, 81, 65, 65, 65, 65,
                65, 67, 65, 65, 65, 65, 97, 87, 81, 65, 65, 65, 61, 61,
            ]),
        )];

        assert_eq!(result_kv_metadata, expected_kv_metadata);

        Spi::run("DROP TABLE workers; DROP TYPE worker, person;").unwrap();
    }

    #[pg_test]
    fn test_parquet_column_stats() {
        let series_start = 10;
        let series_end = 89;
        let row_group_size = 10;

        // set timezone to UTC
        Spi::run("set timezone to 'UTC';").unwrap();

        let ddls = format!(
            "
            create type person AS (id int, name text);
            create table column_stats_test (
                a smallint,
                b int,
                c bigint,
                d float4,
                e float8,
                f numeric(5,2),
                g numeric(12,2),
                h numeric(25,2),
                i numeric(40,2),
                j numeric,
                k text,
                l varchar,
                m date,
                n timestamp,
                o timestamptz,
                p time,
                q timetz,
                r oid,
                s bool,
                t \"char\",
                u bytea,
                u_escaped bytea,
                v int[],
                w person,
                x point,
                y json,
                z jsonb,
                zz uuid);

            -- default are all nulls
            insert into column_stats_test values (default);

            insert into column_stats_test
             select i::smallint,
                    i::int,
                    i::bigint,
                    ('12.' || i)::float4,
                    ('12.' || i)::float8,
                    ('12.' || i)::numeric(5,2),
                    ('12.' || i)::numeric(12,2),
                    ('12.' || i)::numeric(25,2),
                    ('12.' || i)::numeric(40,2),
                    ('12.' || i)::numeric,
                    ('CrunchyData' || i)::text,
                    ('CrunchyData' || i)::varchar,
                    ('01-02-20' || i)::date,
                    ('01-02-20' || i)::timestamp,
                    ('01-02-20' || i || ' 00:00:00-03:00')::timestamptz,
                    ('01:02:' || i % 60)::time,
                    ('01:02:' || i % 60 || '+03:00')::timetz,
                    i::oid,
                    (i % 2 = 0)::bool,
                    chr(i)::\"char\",
                    ('\\x0102' || i)::bytea,
                    ('\\011\\\\\'\'' || encode(chr(i)::bytea, 'escape'))::bytea,
                    array[i, i + 1, i + 2]::int[],
                    row(i, 'CrunchyData' || i)::person,
                    point(i, i + 1)::point,
                    ('{{\"key\":' || i || '}}')::json,
                    ('{{\"key\":' || i || '}}')::jsonb,
                    ('041761f3-d843-49c7-bbd3-c50c86ec34' || i)::uuid
             from generate_series({series_start}, {series_end}) i;

            -- default are all nulls
            insert into column_stats_test values (default);

            copy column_stats_test to '{LOCAL_TEST_FILE_PATH}' with (row_group_size {row_group_size}, field_ids auto);
        "
        );
        Spi::run(&ddls).unwrap();

        let parquet_column_stats_command = format!(
            "select * from parquet.column_stats('{}') order by field_id;",
            LOCAL_TEST_FILE_PATH
        );
        let result_column_stats = Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(&parquet_column_stats_command, None, &[])
                .unwrap();

            for row in tup_table {
                let column_id = row["column_id"].value::<i32>().unwrap().unwrap();
                let field_id = row["field_id"].value::<i32>().unwrap().unwrap();
                let stats_min = row["stats_min"].value::<String>().unwrap();
                let stats_max = row["stats_max"].value::<String>().unwrap();
                let null_count = row["stats_null_count"].value::<i64>().unwrap();
                let distinct_count = row["stats_distinct_count"].value::<i64>().unwrap();

                results.push((
                    column_id,
                    field_id,
                    stats_min,
                    stats_max,
                    null_count,
                    distinct_count,
                ));
            }

            results
        });

        // reset timezone
        Spi::run("reset timezone;").unwrap();

        let expected_column_stats = vec![
            (0, 0, Some("10".into()), Some("89".into()), Some(2), None),
            (1, 1, Some("10".into()), Some("89".into()), Some(2), None),
            (2, 2, Some("10".into()), Some("89".into()), Some(2), None),
            (
                3,
                3,
                Some("12.1".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                4,
                4,
                Some("12.1".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                5,
                5,
                Some("12.10".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                6,
                6,
                Some("12.10".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                7,
                7,
                Some("12.10".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                8,
                8,
                Some("12.10".into()),
                Some("12.89".into()),
                Some(2),
                None,
            ),
            (
                9,
                9,
                Some("12.100000000".into()),
                Some("12.890000000".into()),
                Some(2),
                None,
            ),
            (
                10,
                10,
                Some("CrunchyData10".into()),
                Some("CrunchyData89".into()),
                Some(2),
                None,
            ),
            (
                11,
                11,
                Some("CrunchyData10".into()),
                Some("CrunchyData89".into()),
                Some(2),
                None,
            ),
            (
                12,
                12,
                Some("2010-01-02".into()),
                Some("2089-01-02".into()),
                Some(2),
                None,
            ),
            (
                13,
                13,
                Some("2010-01-02 00:00:00".into()),
                Some("2089-01-02 00:00:00".into()),
                Some(2),
                None,
            ),
            (
                14,
                14,
                Some("2010-01-02 03:00:00+00".into()),
                Some("2089-01-02 03:00:00+00".into()),
                Some(2),
                None,
            ),
            (
                15,
                15,
                Some("01:02:00".into()),
                Some("01:02:59".into()),
                Some(2),
                None,
            ),
            (
                16,
                16,
                Some("22:02:00+00".into()),
                Some("22:02:59+00".into()),
                Some(2),
                None,
            ),
            (17, 17, Some("10".into()), Some("89".into()), Some(2), None),
            (
                18,
                18,
                Some("false".into()),
                Some("true".into()),
                Some(2),
                None,
            ),
            (19, 19, Some("\n".into()), Some("Y".into()), Some(2), None),
            (
                20,
                20,
                Some("\\x010210".into()),
                Some("\\x010289".into()),
                Some(2),
                None,
            ),
            (
                21,
                21,
                Some("\\x095C270A".into()),
                Some("\\x095C2759".into()),
                Some(2),
                None,
            ),
            (22, 23, Some("10".into()), Some("91".into()), Some(2), None),
            (23, 25, Some("10".into()), Some("89".into()), Some(2), None),
            (
                24,
                26,
                Some("CrunchyData10".into()),
                Some("CrunchyData89".into()),
                Some(2),
                None,
            ),
            (
                25,
                27,
                Some("(10,11)".into()),
                Some("(89,90)".into()),
                Some(2),
                None,
            ),
            (
                26,
                28,
                Some("{\"key\":10}".into()),
                Some("{\"key\":89}".into()),
                Some(2),
                None,
            ),
            (
                27,
                29,
                Some("{\"key\":10}".into()),
                Some("{\"key\":89}".into()),
                Some(2),
                None,
            ),
            (
                28,
                30,
                Some("041761f3-d843-49c7-bbd3-c50c86ec3410".into()),
                Some("041761f3-d843-49c7-bbd3-c50c86ec3489".into()),
                Some(2),
                None,
            ),
        ];

        assert_eq!(result_column_stats, expected_column_stats);

        Spi::run("DROP TABLE column_stats_test; DROP TYPE person;").unwrap();
    }
}
