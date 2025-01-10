#[pgrx::pg_schema]
mod tests {
    use std::io::Write;

    use pgrx::{pg_test, Spi};

    use crate::pgrx_tests::common::TestTable;

    #[pg_test]
    fn test_s3_from_env() {
        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

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
    fn test_s3_from_config_file() {
        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        // remove these to make sure the config file is used
        let access_key_id = std::env::var("AWS_ACCESS_KEY_ID").unwrap();
        std::env::remove_var("AWS_ACCESS_KEY_ID");
        let secret_access_key = std::env::var("AWS_SECRET_ACCESS_KEY").unwrap();
        std::env::remove_var("AWS_SECRET_ACCESS_KEY");
        let region = std::env::var("AWS_REGION").unwrap();
        std::env::remove_var("AWS_REGION");
        let endpoint = std::env::var("AWS_ENDPOINT_URL").unwrap();
        std::env::remove_var("AWS_ENDPOINT_URL");

        let profile = "pg_parquet_test";

        // create a config file
        let aws_config_file_content = format!(
            "[profile {profile}]\n\
            region={region}\n\
            aws_access_key_id={access_key_id}\n\
            aws_secret_access_key={secret_access_key}\n\
            endpoint_url={endpoint}\n"
        );
        std::env::set_var("AWS_PROFILE", profile);

        let aws_config_file_path = "/tmp/pg_parquet_aws_config";
        std::env::set_var("AWS_CONFIG_FILE", aws_config_file_path);

        let mut aws_config_file = std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(aws_config_file_path)
            .unwrap();

        aws_config_file
            .write_all(aws_config_file_content.as_bytes())
            .unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        // remove the config file
        std::fs::remove_file(aws_config_file_path).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_s3_wrong_access_key_id() {
        std::env::set_var("AWS_ACCESS_KEY_ID", "wrong_access_key_id");

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_s3_wrong_secret_access_key() {
        std::env::set_var("AWS_SECRET_ACCESS_KEY", "wrong_secret_access_key");

        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
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
    fn test_s3_write_wrong_bucket() {
        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}';",
            s3_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "404 Not Found")]
    fn test_s3_read_wrong_bucket() {
        let s3_uri = "s3://randombucketwhichdoesnotexist/pg_parquet_test.parquet";

        let create_table_command = "CREATE TABLE test_table (a int);";
        Spi::run(create_table_command).unwrap();

        let copy_from_command = format!("COPY test_table FROM '{}';", s3_uri);
        Spi::run(copy_from_command.as_str()).unwrap();
    }

    #[pg_test]
    fn test_s3_temporary_token() {
        let test_bucket_name: String =
            std::env::var("AWS_S3_TEST_BUCKET").expect("AWS_S3_TEST_BUCKET not found");

        // remove these to make sure the config file is used
        let access_key_id = std::env::var("AWS_ACCESS_KEY_ID").unwrap();
        std::env::remove_var("AWS_ACCESS_KEY_ID");
        let secret_access_key = std::env::var("AWS_SECRET_ACCESS_KEY").unwrap();
        std::env::remove_var("AWS_SECRET_ACCESS_KEY");
        let region = std::env::var("AWS_REGION").unwrap();
        std::env::remove_var("AWS_REGION");
        let endpoint = std::env::var("AWS_ENDPOINT_URL").unwrap();
        std::env::remove_var("AWS_ENDPOINT_URL");

        let profile = "pg_parquet_test";

        // create a config file
        let aws_config_file_content = format!(
            "[profile {profile}-source]\n\
            aws_access_key_id={access_key_id}\n\
            aws_secret_access_key={secret_access_key}\n\
            \n\
            [profile {profile}]\n\
            region={region}\n\
            source_profile={profile}-source\n\
            role_arn=arn:aws:iam::123456789012:dummy\n\
            endpoint_url={endpoint}\n"
        );
        std::env::set_var("AWS_PROFILE", profile);

        let aws_config_file_path = "/tmp/pg_parquet_aws_config";
        std::env::set_var("AWS_CONFIG_FILE", aws_config_file_path);

        let mut aws_config_file = std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(aws_config_file_path)
            .unwrap();

        aws_config_file
            .write_all(aws_config_file_content.as_bytes())
            .unwrap();

        let s3_uri = format!("s3://{}/pg_parquet_test.parquet", test_bucket_name);

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();

        // remove the config file
        std::fs::remove_file(aws_config_file_path).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "unsupported s3 uri")]
    fn test_s3_unsupported_uri() {
        let cloudflare_s3_uri = "https://ACCOUNT_ID.r2.cloudflarestorage.com/bucket".into();

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(cloudflare_s3_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    fn test_azure_blob_from_env() {
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
        let test_account_name: String =
            std::env::var("AZURE_STORAGE_ACCOUNT").expect("AZURE_STORAGE_ACCOUNT not found");

        let azure_blob_uri = format!(
            "https://{}.blob.core.windows.net/nonexistentcontainer",
            test_account_name
        );

        let copy_to_command = format!(
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}' WITH (format parquet);;",
            azure_blob_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    fn test_azure_read_write_sas() {
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
            "COPY (SELECT i FROM generate_series(1,10) i) TO '{}' WITH (format parquet);;",
            azure_blob_uri
        );
        Spi::run(copy_to_command.as_str()).unwrap();
    }

    #[pg_test]
    #[should_panic(expected = "403 Forbidden")]
    fn test_azure_read_only_sas() {
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
    #[should_panic(expected = "unsupported azure blob storage uri")]
    fn test_azure_unsupported_uri() {
        let fabric_azure_blob_uri = "https://ACCOUNT.dfs.fabric.microsoft.com".into();

        let test_table = TestTable::<i32>::new("int4".into()).with_uri(fabric_azure_blob_uri);

        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "unsupported scheme gs in uri gs://testbucket")]
    fn test_unsupported_uri() {
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("gs://testbucket".to_string());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }

    #[pg_test]
    #[should_panic(expected = "unrecognized uri dummy://testbucket")]
    fn test_unrecognized_uri() {
        let test_table =
            TestTable::<i32>::new("int4".into()).with_uri("dummy://testbucket".to_string());
        test_table.insert("INSERT INTO test_expected (a) VALUES (1), (2), (null);");
        test_table.assert_expected_and_result_rows();
    }
}
