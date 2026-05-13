#[pgrx::pg_schema]
mod tests {
    use pgrx::{pg_sys::Timestamp, pg_test, Spi};

    use crate::pgrx_tests::common::TestTable;

    fn object_store_cache_clear() {
        Spi::run("SELECT parquet_test.object_store_cache_clear();").unwrap();
    }

    fn object_store_cache_items() -> Vec<(String, String, String, Option<String>, Option<Timestamp>)> {
        Spi::connect(|client| {
            let mut results = Vec::new();
            let tup_table = client
                .select(
                    "SELECT * FROM parquet_test.object_store_cache_items() ORDER BY 1,2,3,4,5;",
                    None,
                    &[],
                )
                .unwrap();

            for row in tup_table {
                let scheme: String = row.get_by_name("scheme").unwrap().unwrap();
                let bucket: String = row.get_by_name("bucket").unwrap().unwrap();
                let role_oid: String = row.get_by_name("role_oid").unwrap().unwrap();
                let server_oid: Option<String> = row.get_by_name("server_oid").unwrap();
                let expire_at: Option<Timestamp> = row.get_by_name("expire_at").unwrap();

                results.push((scheme, bucket, role_oid, server_oid, expire_at));
            }

            results
        })
    }

    fn object_store_expire_item(bucket: &str) {
        Spi::run(&format!(
            "SELECT parquet_test.object_store_cache_expire_bucket('{}');",
            bucket
        ))
        .unwrap();
    }

    fn setup_s3_foreign_server(server_name: &str, scope: &str, key_id: &str, secret: &str) {
        let region = std::env::var("AWS_REGION").unwrap_or_else(|_| "us-east-1".to_string());
        let endpoint = std::env::var("AWS_ENDPOINT_URL").expect("AWS_ENDPOINT_URL not found");
        let allow_http = if endpoint.starts_with("http://") {
            ", allow_http 'true'"
        } else {
            ""
        };

        Spi::run(&format!(
            "DROP SERVER IF EXISTS {server_name} CASCADE;
             CREATE SERVER {server_name} TYPE 's3' FOREIGN DATA WRAPPER parquet \
             OPTIONS (scope '{scope}', region '{region}', endpoint '{endpoint}'{allow_http});
             CREATE USER MAPPING FOR CURRENT_USER SERVER {server_name} \
             OPTIONS (key_id '{key_id}', secret '{secret}');"
        ))
        .unwrap();
    }

    fn setup_s3_foreign_server_for_user(
        server_name: &str,
        scope: &str,
        key_id: &str,
        secret: &str,
        username: &str,
    ) {
        let region = std::env::var("AWS_REGION").unwrap_or_else(|_| "us-east-1".to_string());
        let endpoint = std::env::var("AWS_ENDPOINT_URL").expect("AWS_ENDPOINT_URL not found");
        let allow_http = if endpoint.starts_with("http://") {
            ", allow_http 'true'"
        } else {
            ""
        };

        Spi::run(&format!(
            "DROP SERVER IF EXISTS {server_name} CASCADE;
             CREATE SERVER {server_name} TYPE 's3' FOREIGN DATA WRAPPER parquet \
             OPTIONS (scope '{scope}', region '{region}', endpoint '{endpoint}'{allow_http});
             CREATE USER MAPPING FOR {username} SERVER {server_name} \
             OPTIONS (key_id '{key_id}', secret '{secret}');
             GRANT USAGE ON FOREIGN SERVER {server_name} TO {username};"
        ))
        .unwrap();
    }

    fn default_s3_credentials() -> (String, String) {
        (
            std::env::var("AWS_ACCESS_KEY_ID").expect("AWS_ACCESS_KEY_ID not found"),
            std::env::var("AWS_SECRET_ACCESS_KEY").expect("AWS_SECRET_ACCESS_KEY not found"),
        )
    }

    #[pg_test]
    fn test_s3_from_env() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server("pg_parquet_s3_test", &test_bucket_name, &key_id, &secret);

        let s3_uris = [
            format!("s3://{}/pg_parquet_test.parquet", test_bucket_name),
            format!(
                "https://s3.amazonaws.com/{}/pg_parquet_test.parquet",
                test_bucket_name
            ),
            format!(
                "https://{}.s3.amazonaws.com/pg_parquet_test.parquet",
                test_bucket_name
            ),
        ];

        for s3_uri in s3_uris {
            let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

            test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
            test_table.assert_expected_and_result_rows();
        }
    }

    #[pg_test]
    fn test_s3_foreign_server_catalog() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server("pg_parquet_s3_catalog", &test_bucket_name, &key_id, &secret);

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);
        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_s3_wrong_access_key_id() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (_, secret) = default_s3_credentials();
        setup_s3_foreign_server(
            "pg_parquet_s3_wrong_key",
            &test_bucket_name,
            "wrong_access_key_id",
            &secret,
        );

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_s3_wrong_secret_access_key() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, _) = default_s3_credentials();
        setup_s3_foreign_server(
            "pg_parquet_s3_wrong_secret",
            &test_bucket_name,
            &key_id,
            "wrong_secret_access_key",
        );

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY from a remote uri")]
    fn test_s3_no_read_access() {
        object_store_cache_clear();

        Spi::run("CREATE USER regular_user;").unwrap();
        Spi::run("GRANT parquet_object_store_write TO regular_user;").unwrap();
        Spi::run("GRANT ALL ON SCHEMA public TO regular_user;").unwrap();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server_for_user(
            "pg_parquet_s3_no_read",
            &test_bucket_name,
            &key_id,
            &secret,
            "regular_user",
        );

        Spi::run("SET SESSION AUTHORIZATION regular_user;").unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");

        let copy_to_command = format!(
            "COPY (SELECT a FROM generate_series(1,10) a) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();

        let copy_from_command = format!("COPY test_expected FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "permission denied to COPY to a remote uri")]
    fn test_s3_no_write_access() {
        object_store_cache_clear();

        Spi::run("CREATE USER regular_user;").unwrap();
        Spi::run("GRANT parquet_object_store_read TO regular_user;").unwrap();
        Spi::run("GRANT USAGE ON SCHEMA parquet TO regular_user;").unwrap();
        Spi::run("GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA parquet TO regular_user;").unwrap();
        Spi::run("GRANT ALL ON SCHEMA public TO regular_user;").unwrap();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server_for_user(
            "pg_parquet_s3_no_write",
            &test_bucket_name,
            &key_id,
            &secret,
            "regular_user",
        );

        Spi::run("SET SESSION AUTHORIZATION regular_user;").unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let metadata_query = format!("SELECT parquet.metadata('{}');", s3_uri.clone());
        Spi::run(&metadata_query).unwrap();

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");

        let copy_from_command = format!("COPY test_expected FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();

        let copy_to_command = format!(
            "COPY (SELECT a FROM generate_series(1,10) a) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "no parquet S3 foreign server and user mapping match uri")]
    fn test_s3_write_wrong_bucket() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server(
            "pg_parquet_s3_scope",
            &test_bucket_name,
            &key_id,
            &secret,
        );

        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "no parquet S3 foreign server and user mapping match uri")]
    fn test_s3_read_wrong_bucket() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server(
            "pg_parquet_s3_scope_read",
            &test_bucket_name,
            &key_id,
            &secret,
        );

        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let create_table_command = "CREATE TABLE test_table (a int);";
        Spi::run(create_table_command).unwrap();

        let copy_from_command = format!("COPY test_table FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    fn test_s3_temporary_token() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();
        let region = std::env::var("AWS_REGION").unwrap_or_else(|_| "us-east-1".to_string());
        let endpoint = std::env::var("AWS_ENDPOINT_URL").expect("AWS_ENDPOINT_URL not found");
        let allow_http = if endpoint.starts_with("http://") {
            ", allow_http 'true'"
        } else {
            ""
        };

        Spi::run(&format!(
            "DROP SERVER IF EXISTS pg_parquet_s3_token CASCADE;
             CREATE SERVER pg_parquet_s3_token TYPE 's3' FOREIGN DATA WRAPPER parquet \
             OPTIONS (scope '{test_bucket_name}', region '{region}', endpoint '{endpoint}'{allow_http});
             CREATE USER MAPPING FOR CURRENT_USER SERVER pg_parquet_s3_token \
             OPTIONS (key_id '{key_id}', secret '{secret}', session_token 'temporary-token');"
        ))
        .unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        let cache_items = object_store_cache_items();
        assert_eq!(cache_items.len(), 1);
        assert!(cache_items[0].4.is_some());

        object_store_expire_item(&test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    fn cache_scheme_bucket_pairs() -> Vec<(String, String)> {
        object_store_cache_items()
            .into_iter()
            .map(|(scheme, bucket, _, _, _)| (scheme, bucket))
            .collect()
    }

    #[pg_test]
    #[should_panic(expected = "cannot be used in the SERVER's OPTIONS")]
    fn test_s3_rejects_secret_on_server() {
        Spi::run(
            "CREATE SERVER pg_parquet_s3_invalid TYPE 's3' FOREIGN DATA WRAPPER parquet OPTIONS (secret 'leaked');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "no parquet S3 foreign server and user mapping match uri")]
    fn test_s3_missing_user_mapping() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let region = std::env::var("AWS_REGION").unwrap_or_else(|_| "us-east-1".to_string());
        let endpoint = std::env::var("AWS_ENDPOINT_URL").expect("AWS_ENDPOINT_URL not found");
        let allow_http = if endpoint.starts_with("http://") {
            ", allow_http 'true'"
        } else {
            ""
        };

        Spi::run(&format!(
            "DROP SERVER IF EXISTS pg_parquet_s3_no_mapping CASCADE;
             CREATE SERVER pg_parquet_s3_no_mapping TYPE 's3' FOREIGN DATA WRAPPER parquet \
             OPTIONS (scope '{test_bucket_name}', region '{region}', endpoint '{endpoint}'{allow_http});"
        ))
        .unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);
        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_s3_user_isolation() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");
        let (key_id, secret) = default_s3_credentials();

        Spi::run("CREATE USER parquet_user_a;").unwrap();
        Spi::run("CREATE USER parquet_user_b;").unwrap();
        Spi::run("GRANT parquet_object_store_read, parquet_object_store_write TO parquet_user_a;")
            .unwrap();
        Spi::run("GRANT parquet_object_store_read, parquet_object_store_write TO parquet_user_b;")
            .unwrap();
        Spi::run("GRANT ALL ON SCHEMA public TO parquet_user_a, parquet_user_b;").unwrap();

        setup_s3_foreign_server_for_user(
            "pg_parquet_s3_user_a",
            &test_bucket_name,
            &key_id,
            &secret,
            "parquet_user_a",
        );
        setup_s3_foreign_server_for_user(
            "pg_parquet_s3_user_b",
            &test_bucket_name,
            &key_id,
            &secret,
            "parquet_user_b",
        );

        let s3_uri = format!("s3://{}/pg_parquet_user_isolation.parquet", test_bucket_name);

        Spi::run("SET SESSION AUTHORIZATION parquet_user_a;").unwrap();
        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri.clone());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        Spi::run("SET SESSION AUTHORIZATION parquet_user_b;").unwrap();
        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "relative path not allowed")]
    fn test_s3_unsupported_uri() {
        object_store_cache_clear();

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        Spi::run("copy test_table to 'https://ACCOUNT_ID.r2.cloudflarestorage.com/bucket';")
            .unwrap();
    }

    #[pg_test]
    fn test_azure_blob_from_env() {
        object_store_cache_clear();

        // unset AZURE_STORAGE_CONNECTION_STRING to make sure the account name and key are used
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let azure_blob_uris = [
            format!("az://{}/pg_parquet_test.parquet", test_container_name),
            format!("azure://{}/pg_parquet_test.parquet", test_container_name),
            format!(
                "https://{}.blob.core.windows.net/{}",
                test_account_name, test_container_name
            ),
        ];

        for azure_blob_uri in azure_blob_uris {
            let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

            test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
            test_table.assert_expected_and_result_rows();
        }
    }

    #[pg_test]
    fn test_azure_from_config_file() {
        object_store_cache_clear();

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        // remove these to make sure the config file is used
        let account_name = std::env::var("AZURE_STORAGE_ACCOUNT").unwrap();
        std::env::remove_var("AZURE_STORAGE_ACCOUNT");
        let account_key = std::env::var("AZURE_STORAGE_KEY").unwrap();
        std::env::remove_var("AZURE_STORAGE_KEY");
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");

        // create a config file
        let azure_config_file_content = format!(
            "[storage]\n\
            account={account_name}\n\
            key={account_key}"
        );

        let azure_config_file_path = "/tmp/pg_parquet_azure_config";
        std::env::set_var("AZURE_CONFIG_FILE", azure_config_file_path);

        let mut azure_config_file = std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(azure_config_file_path)
            .unwrap();

        azure_config_file
            .write_all(azure_config_file_content.as_bytes())
            .unwrap();

        let azure_blob_uri = format!("az://{}/pg_parquet_test.parquet", test_container_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        // remove the config file
        std::fs::remove_file(azure_config_file_path).unwrap();
    }

    #[pg_test]
    fn test_azure_from_env_via_connection_string() {
        object_store_cache_clear();

        // unset AZURE_STORAGE_ACCOUNT AND AZURE_STORAGE_KEY to make sure the connection string is used
        std::env::remove_var("AZURE_STORAGE_ACCOUNT");
        std::env::remove_var("AZURE_STORAGE_KEY");

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let azure_blob_uri = format!("az://{}/pg_parquet_test.parquet", test_container_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_azure_from_config_via_connection_string() {
        object_store_cache_clear();

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        // remove these to make sure the config file is used
        std::env::remove_var("AZURE_STORAGE_ACCOUNT");
        std::env::remove_var("AZURE_STORAGE_KEY");
        let connection_string = std::env::var("AZURE_STORAGE_CONNECTION_STRING").unwrap();
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");

        // create a config file
        let azure_config_file_content =
            format!("[storage]\nconnection_string = {connection_string}\n");

        let azure_config_file_path = "/tmp/pg_parquet_azure_config";
        std::env::set_var("AZURE_CONFIG_FILE", azure_config_file_path);

        let mut azure_config_file = std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(azure_config_file_path)
            .unwrap();

        azure_config_file
            .write_all(azure_config_file_content.as_bytes())
            .unwrap();

        let azure_blob_uri = format!("az://{}/pg_parquet_test.parquet", test_container_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        // remove the config file
        std::fs::remove_file(azure_config_file_path).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "Account must be specified")]
    fn test_azure_no_storage_account() {
        object_store_cache_clear();

        // unset AZURE_STORAGE_CONNECTION_STRING to make sure the account name and key are used
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");

        std::env::remove_var("AZURE_STORAGE_ACCOUNT");

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let azure_blob_uri = format!("az://{}/pg_parquet_test.parquet", test_container_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_azure_wrong_storage_key() {
        object_store_cache_clear();

        // unset AZURE_STORAGE_CONNECTION_STRING to make sure the account name and key are used
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");

        let wrong_account_key = String::from("FFy8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==");
        std::env::set_var("AZURE_STORAGE_KEY", wrong_account_key);

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let azure_blob_uri = format!(
            "https://{}.blob.core.windows.net/{}",
            test_account_name, test_container_name
        );

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "404 Not Found")]
    fn test_azure_write_wrong_container() {
        object_store_cache_clear();

        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let azure_blob_uri = format!(
            "https://{}.blob.core.windows.net/nonexistentcontainer",
            test_account_name
        );

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}' WITH (format parquet);",
            azure_blob_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    fn test_azure_read_write_sas() {
        object_store_cache_clear();

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let read_write_sas_token = std::env::var("AZURE_TEST_READ_WRITE_SAS")
            .expect("AZURE_TEST_READ_WRITE_SAS not found");

        // remove account key and connection string to make sure the sas token is used
        std::env::remove_var("AZURE_STORAGE_KEY");
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");
        std::env::set_var("AZURE_STORAGE_SAS_TOKEN", read_write_sas_token);

        let azure_blob_uri = format!(
            "https://{}.blob.core.windows.net/{}",
            test_account_name, test_container_name
        );

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}' WITH (format parquet);",
            azure_blob_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_azure_read_only_sas() {
        object_store_cache_clear();

        let test_container_name: String = std::env::var("AZURE_TEST_CONTAINER_NAME")
            .expect("AZURE_TEST_CONTAINER_NAME not found");

        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let read_only_sas_token: String =
            std::env::var("AZURE_TEST_READ_ONLY_SAS").expect("AZURE_TEST_READ_ONLY_SAS not found");

        // remove account key and connection string to make sure the sas token is used
        std::env::remove_var("AZURE_STORAGE_KEY");
        std::env::remove_var("AZURE_STORAGE_CONNECTION_STRING");
        std::env::set_var("AZURE_STORAGE_SAS_TOKEN", read_only_sas_token);

        let azure_blob_uri = format!(
            "https://{}.blob.core.windows.net/{}",
            test_account_name, test_container_name
        );

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}' WITH (format parquet);",
            azure_blob_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "could not open file")]
    fn test_azure_unsupported_uri() {
        object_store_cache_clear();

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        Spi::run("copy test_table from 'https://ACCOUNT.dfs.fabric.microsoft.com';").unwrap();
    }

    #[pg_test]
    fn test_http_uri() {
        object_store_cache_clear();

        let http_endpoint: String =
            std::env::var("HTTP_ENDPOINT").expect("HTTP_ENDPOINT not found");

        let http_uris = [
            format!("{http_endpoint}/pg_parquet_test.parquet"),
            format!("{http_endpoint}/dummy/pg_parquet_test"),
        ];

        for http_uri in http_uris {
            let test_table = TestTable::<i32>::new("int4".into()).with_uri(http_uri);

            test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
            test_table.assert_expected_and_result_rows();
        }
    }

    fn setup_gcs_foreign_server(server_name: &str, scope: &str) {
        let hmac_key_id = std::env::var("GOOGLE_HMAC_KEY_ID").ok();
        let hmac_secret = std::env::var("GOOGLE_HMAC_SECRET").ok();
        let sa_key = std::env::var("GOOGLE_SERVICE_ACCOUNT_KEY").ok();
        let sa_path = std::env::var("GOOGLE_SERVICE_ACCOUNT_PATH").ok();

        let mapping_options = match (hmac_key_id, hmac_secret, sa_key, sa_path) {
            (Some(k), Some(s), _, _) => {
                format!("key_id '{}', secret '{}'", k, s)
            }
            (_, _, Some(key), _) => format!("service_account_key '{}'", key.replace('\'', "''")),
            (_, _, _, Some(path)) => format!("service_account_path '{}'", path),
            _ => panic!("GCS credentials not provided: set GOOGLE_HMAC_KEY_ID/GOOGLE_HMAC_SECRET or GOOGLE_SERVICE_ACCOUNT_KEY or GOOGLE_SERVICE_ACCOUNT_PATH"),
        };

        Spi::run(&format!(
            "DROP SERVER IF EXISTS {server_name} CASCADE;
             CREATE SERVER {server_name} TYPE 'gcs' FOREIGN DATA WRAPPER parquet \
             OPTIONS (scope '{scope}');
             CREATE USER MAPPING FOR CURRENT_USER SERVER {server_name} \
             OPTIONS ({mapping_options});"
        ))
        .unwrap();
    }

    #[pg_test]
    fn test_gcs_foreign_server_catalog() {
        object_store_cache_clear();

        let test_bucket_name: String =
            std::env::var("GOOGLE_TEST_BUCKET").expect("GOOGLE_TEST_BUCKET not found");
        setup_gcs_foreign_server("pg_parquet_gcs_catalog", &test_bucket_name);

        let gcs_uri = format!("gs://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(gcs_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "no parquet gcs foreign server and user mapping match uri")]
    fn test_gcs_write_wrong_bucket() {
        object_store_cache_clear();

        let s3_uri = "gs://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "no parquet gcs foreign server and user mapping match uri")]
    fn test_gcs_read_wrong_bucket() {
        object_store_cache_clear();

        let gcs_uri = "gs://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let create_table_command = "CREATE TABLE test_table (a int);";
        Spi::run(create_table_command).unwrap();

        let copy_from_command = format!("COPY test_table FROM '{}';", gcs_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "cannot be used in the SERVER's OPTIONS")]
    fn test_gcs_rejects_service_account_on_server() {
        Spi::run(
            "CREATE SERVER pg_parquet_gcs_invalid TYPE 'gcs' FOREIGN DATA WRAPPER parquet \
             OPTIONS (service_account_key '{}');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "invalid option \"region\" for parquet foreign server")]
    fn test_gcs_rejects_region_on_server() {
        Spi::run(
            "CREATE SERVER pg_parquet_gcs_region TYPE 'gcs' FOREIGN DATA WRAPPER parquet \
             OPTIONS (region 'us-east-1');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "cannot mix HMAC")]
    fn test_gcs_rejects_mixed_credentials() {
        Spi::run(
            "DROP SERVER IF EXISTS pg_parquet_gcs_mixed CASCADE;
             CREATE SERVER pg_parquet_gcs_mixed TYPE 'gcs' FOREIGN DATA WRAPPER parquet;
             CREATE USER MAPPING FOR CURRENT_USER SERVER pg_parquet_gcs_mixed \
             OPTIONS (key_id 'k', secret 's', service_account_path '/tmp/sa.json');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "service_account_key or service_account_path, not both")]
    fn test_gcs_rejects_both_service_account_fields() {
        Spi::run(
            "DROP SERVER IF EXISTS pg_parquet_gcs_two_sa CASCADE;
             CREATE SERVER pg_parquet_gcs_two_sa TYPE 'gcs' FOREIGN DATA WRAPPER parquet;
             CREATE USER MAPPING FOR CURRENT_USER SERVER pg_parquet_gcs_two_sa \
             OPTIONS (service_account_key '{}', service_account_path '/tmp/sa.json');",
        )
        .unwrap();
    }

    fn count_user_mapping_for_server(server_name: &str) -> i64 {
        Spi::get_one_with_args::<i64>(
            "SELECT count(*)::bigint FROM pg_catalog.pg_user_mappings \
             WHERE srvname = $1 AND usename = current_user;",
            &[server_name.into()],
        )
        .expect("count user mapping")
        .expect("non-null count")
    }

    fn server_type(server_name: &str) -> String {
        Spi::get_one_with_args::<String>(
            "SELECT srvtype::text FROM pg_catalog.pg_foreign_server WHERE srvname = $1;",
            &[server_name.into()],
        )
        .expect("server type")
        .expect("non-null server type")
    }

    #[pg_test]
    fn test_create_simple_secret_s3() {
        let name: String = Spi::get_one(
            "SELECT parquet.create_simple_secret(\
                'S3', key_id := 'AKIATESTKEY', secret := 'sek', region := 'us-east-1');",
        )
        .expect("simple secret")
        .expect("non-null name");
        assert_eq!(name, "simple_s3_secret");
        assert_eq!(server_type(&name), "s3");
        assert_eq!(count_user_mapping_for_server(&name), 1);

        let name2: String = Spi::get_one(
            "SELECT parquet.create_simple_secret(\
                's3', 'AKIATESTKEY2', 'sek2', region => 'us-east-2');",
        )
        .expect("simple secret")
        .expect("non-null name");
        assert_eq!(name2, "simple_s3_secret_1");
    }

    #[pg_test]
    fn test_create_simple_secret_gcs_hmac() {
        let name: String = Spi::get_one(
            "SELECT parquet.create_simple_secret('gcs', 'GOOGTESTKEY', 'sek');",
        )
        .expect("simple secret")
        .expect("non-null name");
        assert_eq!(name, "simple_gcs_secret");
        assert_eq!(server_type(&name), "gcs");
        assert_eq!(count_user_mapping_for_server(&name), 1);
    }

    #[pg_test]
    fn test_create_simple_secret_gcs_service_account() {
        let name: String = Spi::get_one(
            "SELECT parquet.create_simple_secret('gcs', service_account_path => '/tmp/sa.json');",
        )
        .expect("simple secret")
        .expect("non-null name");
        assert_eq!(name, "simple_gcs_secret");
        assert_eq!(server_type(&name), "gcs");
        assert_eq!(count_user_mapping_for_server(&name), 1);
    }

    #[pg_test]
    #[should_panic(expected = "type must be 's3' or 'gcs'")]
    fn test_create_simple_secret_invalid_type() {
        Spi::run("SELECT parquet.create_simple_secret('azure', 'k', 's');").unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "service_account_* options are only valid for type='gcs'")]
    fn test_create_simple_secret_s3_rejects_sa() {
        Spi::run(
            "SELECT parquet.create_simple_secret('s3', 'k', 's', service_account_path => '/tmp/sa.json');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "cannot mix HMAC and service-account")]
    fn test_create_simple_secret_gcs_rejects_mixed() {
        Spi::run(
            "SELECT parquet.create_simple_secret('gcs', 'k', 's', service_account_path => '/tmp/sa.json');",
        )
        .unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "type='gcs' requires HMAC")]
    fn test_create_simple_secret_gcs_requires_credentials() {
        Spi::run("SELECT parquet.create_simple_secret('gcs');").unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "relative path not allowed")]
    fn test_copy_to_unrecognized_scheme() {
        object_store_cache_clear();

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        Spi::run("copy test_table to 'dummy://testbucket/dummy.parquet';").unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "relative path not allowed")]
    fn test_copy_to_non_parquet_uri() {
        object_store_cache_clear();

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        Spi::run("copy test_table to 's3://testbucket/dummy.csv';").unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "could not open file")]
    fn test_copy_from_non_parquet_uri() {
        object_store_cache_clear();

        let create_table = "create table test_table(id int);";
        Spi::run(create_table).unwrap();

        Spi::run("copy test_table from 's3://testbucket/dummy.csv';").unwrap();
    }

    #[pg_test]
    fn test_object_store_cache() {
        object_store_cache_clear();

        let (key_id, secret) = default_s3_credentials();
        setup_s3_foreign_server("pg_parquet_cache_1", "testbucket", &key_id, &secret);
        setup_s3_foreign_server("pg_parquet_cache_2", "testbucket2", &key_id, &secret);

        // s3 scheme and bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("s3://testbucket/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
            ]
        );

        // s3 scheme and same bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("s3://testbucket/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
            ]
        );

        // s3 scheme and different bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("s3://testbucket2/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string())
            ]
        );

        // az scheme and container
        let test_table = TestTable::<i32>::new("int4".into())
            .with_uri("az://testcontainer/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string())
            ]
        );

        // az scheme and same container
        let test_table = TestTable::<i32>::new("int4".into())
            .with_uri("az://testcontainer/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string())
            ]
        );

        // az scheme and different container
        let test_table = TestTable::<i32>::new("int4".into())
            .with_uri("az://testcontainer2/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // gs scheme and bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("gs://testbucket/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // gs scheme and same bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("gs://testbucket/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // gs scheme and different bucket
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("gs://testbucket2/test1.parquet".into());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket2".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // https scheme and base uri
        let http_uri =
            "https://d37ci6vzurychx.cloudfront.net/trip-data/yellow_tripdata_2024-03.parquet";
        Spi::run(format!("SELECT * FROM parquet.schema('{http_uri}')").as_str()).unwrap();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket2".to_string()),
                ("Http".to_string(), "https://d37ci6vzurychx.cloudfront.net".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // https scheme and same base uri
        let http_uri =
            "https://d37ci6vzurychx.cloudfront.net/trip-data/yellow_tripdata_2024-04.parquet";
        Spi::run(format!("SELECT * FROM parquet.schema('{http_uri}')").as_str()).unwrap();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket2".to_string()),
                ("Http".to_string(), "https://d37ci6vzurychx.cloudfront.net".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );

        // https scheme and different base uri
        let http_uri = "https://www.filesampleshub.com/download/code/parquet/sample1.parquet";
        Spi::run(format!("SELECT * FROM parquet.schema('{http_uri}')").as_str()).unwrap();

        assert_eq!(
            cache_scheme_bucket_pairs(),
            vec![
                ("AmazonS3".to_string(), "testbucket".to_string()),
                ("AmazonS3".to_string(), "testbucket2".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket".to_string()),
                ("GoogleCloudStorage".to_string(), "testbucket2".to_string()),
                ("Http".to_string(), "https://d37ci6vzurychx.cloudfront.net".to_string()),
                ("Http".to_string(), "https://www.filesampleshub.com".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer".to_string()),
                ("MicrosoftAzure".to_string(), "testcontainer2".to_string())
            ]
        );
    }
}
