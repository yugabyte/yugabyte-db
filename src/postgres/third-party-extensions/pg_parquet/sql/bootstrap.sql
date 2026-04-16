-- create roles for parquet object store read and write if they do not exist
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'parquet_object_store_read') THEN
        CREATE ROLE parquet_object_store_read;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'parquet_object_store_write') THEN
        CREATE ROLE parquet_object_store_write;
    END IF;
END $$;

-- error if the schema already exists
CREATE SCHEMA parquet;
REVOKE ALL ON SCHEMA parquet FROM public;
GRANT USAGE ON SCHEMA parquet TO public;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA parquet TO public;
