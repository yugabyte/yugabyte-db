-- we are introducing this operator in multiple version through hotfixes
-- therefore we need to ensure that it's not created multiple times
DO
$$
BEGIN

    IF (
        SELECT COUNT(*) = 0 FROM pg_operator WHERE oprname = '=#=' AND
        oprnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'helio_api_internal')
    ) THEN

        #include "pg_documentdb/sql/operators/bson_unique_shard_path_operators--0.24-0.sql";

    END IF;

END;
$$;