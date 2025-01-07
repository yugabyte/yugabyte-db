-- we are introducing this operator class in multiple version through hotfixes
-- therefore we need to ensure that it's not created multiple times
DO
$$
BEGIN

    IF (
        SELECT COUNT(*) = 0 FROM pg_opclass WHERE opcname = 'bson_rum_unique_shard_path_ops' AND
        opcnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'helio_api_internal')
    ) THEN

        #include "pg_documentdb/sql/schema/unique_shard_path_operator_class--0.24-0.sql";

    END IF;

END;
$$;