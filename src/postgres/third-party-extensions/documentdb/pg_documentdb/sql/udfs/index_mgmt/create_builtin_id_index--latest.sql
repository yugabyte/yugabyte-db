
DROP FUNCTION IF EXISTS __API_SCHEMA_INTERNAL__.create_builtin_id_index;
CREATE OR REPLACE FUNCTION __API_SCHEMA_INTERNAL__.create_builtin_id_index(
    collection_id bigint, register_id_index bool default true
)
RETURNS void AS $fn$
DECLARE
    final_record record;
BEGIN
    /* sync collection_pk_%s with index.h */
    EXECUTE format($$
        ALTER TABLE __API_DATA_SCHEMA__.documents_%s
        ADD CONSTRAINT collection_pk_%s
        PRIMARY KEY (shard_key_value, object_id)
    $$, collection_id, collection_id);

    IF register_id_index THEN
        PERFORM __API_SCHEMA_INTERNAL__.record_id_index(collection_id);
    END IF;
END;
$fn$
LANGUAGE plpgsql;
