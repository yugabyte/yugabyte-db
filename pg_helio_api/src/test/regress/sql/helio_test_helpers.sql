CREATE SCHEMA helio_test_helpers;

-- Check if recreating the extension works
DROP EXTENSION IF EXISTS pg_helio_api;

-- Install the latest available helio_api version
CREATE EXTENSION pg_helio_api CASCADE;

-- binary version should return the installed version after recreating the extension
SELECT helio_api.binary_version() = (SELECT REPLACE(extversion, '-', '.') FROM pg_extension where extname = 'pg_helio_core');

-- query helio_api_catalog.collection_indexes for given collection
CREATE OR REPLACE FUNCTION helio_test_helpers.get_collection_indexes(
    p_database_name text,
    p_collection_name text,
    OUT collection_id bigint,
    OUT index_id integer,
    OUT index_spec_as_bson helio_core.bson,
    OUT index_is_valid bool)
RETURNS SETOF RECORD
AS $$
BEGIN
  RETURN QUERY
  SELECT ci.collection_id, ci.index_id,
         helio_api_internal.index_spec_as_bson(ci.index_spec),
         ci.index_is_valid
  FROM helio_api_catalog.collection_indexes AS ci
  WHERE ci.collection_id = (SELECT hc.collection_id FROM helio_api_catalog.collections AS hc
                            WHERE collection_name = p_collection_name AND
                                  database_name = p_database_name)
  ORDER BY ci.index_id;
END;
$$ LANGUAGE plpgsql;

-- query pg_index for the documents table backing given collection
CREATE OR REPLACE FUNCTION helio_test_helpers.get_data_table_indexes (
    p_database_name text,
    p_collection_name text)
RETURNS TABLE (LIKE pg_index)
AS $$
DECLARE
  v_collection_id bigint;
  v_data_table_name text;
BEGIN
  SELECT collection_id INTO v_collection_id
  FROM helio_api_catalog.collections
  WHERE collection_name = p_collection_name AND
        database_name = p_database_name;

  v_data_table_name := format('helio_data.documents_%s', v_collection_id);

  RETURN QUERY
  SELECT * FROM pg_index WHERE indrelid = v_data_table_name::regclass;
END;
$$ LANGUAGE plpgsql;

-- Returns the command (without "CONCURRENTLY" option) used to create given
-- index on a collection.
CREATE FUNCTION helio_test_helpers.helio_index_get_pg_def(
    p_database_name text,
    p_collection_name text,
    p_index_name text)
RETURNS SETOF TEXT
AS
$$
BEGIN
    RETURN QUERY
    SELECT pi.indexdef
    FROM helio_api_catalog.collection_indexes hi,
         helio_api_catalog.collections hc,
         pg_indexes pi
    WHERE hc.database_name = p_database_name AND
          hc.collection_name = p_collection_name AND
          (hi.index_spec).index_name = p_index_name AND
          hi.collection_id = hc.collection_id AND
          pi.indexname = concat('documents_rum_index_', index_id::text) AND
          pi.schemaname = 'helio_data';
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION helio_test_helpers.drop_primary_key(p_database_name text, p_collection_name text)
RETURNS void
AS $$
DECLARE
  v_collection_id bigint;
BEGIN
    SELECT collection_id INTO v_collection_id
    FROM helio_api_catalog.collections
    WHERE collection_name = p_collection_name AND
          database_name = p_database_name;

    DELETE FROM helio_api_catalog.collection_indexes
    WHERE (index_spec).index_key operator(helio_core.=) '{"_id": 1}'::helio_core.bson AND
          collection_id = v_collection_id;
	EXECUTE format('ALTER TABLE helio_data.documents_%s DROP CONSTRAINT collection_pk_%s', v_collection_id, v_collection_id);
END;
$$ LANGUAGE plpgsql;