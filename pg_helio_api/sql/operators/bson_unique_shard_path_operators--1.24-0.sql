-- we are introducing this operator in multiple version through hotfixes
-- therefore we need to ensure that it's not created multiple times
DO
$$
BEGIN

    IF (
        SELECT COUNT(*) = 0 FROM pg_operator WHERE oprname = '=#=' AND
        oprnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'helio_api_internal')
    ) THEN

        CREATE OPERATOR helio_api_internal.=#=
        (
            LEFTARG = __CORE_SCHEMA__.bson,
            RIGHTARG = __CORE_SCHEMA__.bson,
            PROCEDURE = helio_api_internal.bson_unique_shard_path_equal,
            COMMUTATOR = OPERATOR(helio_api_internal.=#=)
        );

    END IF;

END;
$$;