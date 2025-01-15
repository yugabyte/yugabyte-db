CREATE SCHEMA documentdb_test_helpers;

SELECT datname, datcollate, datctype, pg_encoding_to_char(encoding), datlocprovider FROM pg_database;

-- Check if recreating the extension works
DROP EXTENSION IF EXISTS documentdb;

-- Install the latest available documentdb_api version
CREATE EXTENSION documentdb CASCADE;

-- binary version should return the installed version after recreating the extension
SELECT documentdb_api.binary_version() = (SELECT REPLACE(extversion, '-', '.') FROM pg_extension where extname = 'documentdb_core');

-- query documentdb_api_catalog.collection_indexes for given collection
CREATE OR REPLACE FUNCTION documentdb_test_helpers.get_collection_indexes(
    p_database_name text,
    p_collection_name text,
    OUT collection_id bigint,
    OUT index_id integer,
    OUT index_spec_as_bson documentdb_core.bson,
    OUT index_is_valid bool)
RETURNS SETOF RECORD
AS $$
BEGIN
  RETURN QUERY
  SELECT ci.collection_id, ci.index_id,
         documentdb_api_internal.index_spec_as_bson(ci.index_spec),
         ci.index_is_valid
  FROM documentdb_api_catalog.collection_indexes AS ci
  WHERE ci.collection_id = (SELECT hc.collection_id FROM documentdb_api_catalog.collections AS hc
                            WHERE collection_name = p_collection_name AND
                                  database_name = p_database_name)
  ORDER BY ci.index_id;
END;
$$ LANGUAGE plpgsql;

-- query pg_index for the documents table backing given collection
CREATE OR REPLACE FUNCTION documentdb_test_helpers.get_data_table_indexes (
    p_database_name text,
    p_collection_name text)
RETURNS TABLE (LIKE pg_index)
AS $$
DECLARE
  v_collection_id bigint;
  v_data_table_name text;
BEGIN
  SELECT collection_id INTO v_collection_id
  FROM documentdb_api_catalog.collections
  WHERE collection_name = p_collection_name AND
        database_name = p_database_name;

  v_data_table_name := format('documentdb_data.documents_%s', v_collection_id);

  RETURN QUERY
  SELECT * FROM pg_index WHERE indrelid = v_data_table_name::regclass;
END;
$$ LANGUAGE plpgsql;

-- Returns the command (without "CONCURRENTLY" option) used to create given
-- index on a collection.
CREATE FUNCTION documentdb_test_helpers.documentdb_index_get_pg_def(
    p_database_name text,
    p_collection_name text,
    p_index_name text)
RETURNS SETOF TEXT
AS
$$
BEGIN
    RETURN QUERY
    SELECT pi.indexdef
    FROM documentdb_api_catalog.collection_indexes hi,
         documentdb_api_catalog.collections hc,
         pg_indexes pi
    WHERE hc.database_name = p_database_name AND
          hc.collection_name = p_collection_name AND
          (hi.index_spec).index_name = p_index_name AND
          hi.collection_id = hc.collection_id AND
          pi.indexname = concat('documents_rum_index_', index_id::text) AND
          pi.schemaname = 'documentdb_data';
END;
$$
LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION documentdb_test_helpers.drop_primary_key(p_database_name text, p_collection_name text)
RETURNS void
AS $$
DECLARE
  v_collection_id bigint;
BEGIN
    SELECT collection_id INTO v_collection_id
    FROM documentdb_api_catalog.collections
    WHERE collection_name = p_collection_name AND
          database_name = p_database_name;

    DELETE FROM documentdb_api_catalog.collection_indexes
    WHERE (index_spec).index_key operator(documentdb_core.=) '{"_id": 1}'::documentdb_core.bson AND
          collection_id = v_collection_id;
	EXECUTE format('ALTER TABLE documentdb_data.documents_%s DROP CONSTRAINT collection_pk_%s', v_collection_id, v_collection_id);
END;
$$ LANGUAGE plpgsql;
