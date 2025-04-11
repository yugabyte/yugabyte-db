CREATE SCHEMA IF NOT EXISTS documentdb_distributed_test_helpers;

SELECT citus_set_coordinator_host('localhost', current_setting('port')::integer);
SELECT citus_set_node_property('localhost', current_setting('port')::integer, 'shouldhaveshards', true);

/* see the comment written for its definition at create_indexes.c */
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.generate_create_index_arg(
    p_collection_name text,
    p_index_name text,
    p_index_key documentdb_core.bson)
RETURNS documentdb_core.bson LANGUAGE C STRICT AS 'pg_documentdb', $$generate_create_index_arg$$;

-- Returns the command (without "CONCURRENTLY" option) used to create given
-- Mongo index on given collection.
CREATE FUNCTION documentdb_distributed_test_helpers.mongo_index_get_pg_def(
    p_database_name text,
    p_collection_name text,
    p_index_name text)
RETURNS SETOF TEXT
AS
$$
BEGIN
    RETURN QUERY
    SELECT pi.indexdef
    FROM documentdb_api_catalog.collection_indexes mi,
         documentdb_api_catalog.collections mc,
         pg_indexes pi
    WHERE mc.database_name = p_database_name AND
          mc.collection_name = p_collection_name AND
          (mi.index_spec).index_name = p_index_name AND
          mi.collection_id = mc.collection_id AND
          pi.indexname = concat('documents_rum_index_', index_id::text) AND
          pi.schemaname = 'documentdb_data';
END;
$$
LANGUAGE plpgsql;


-- query documentdb_api_catalog.collection_indexes for given collection
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.get_collection_indexes(
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
  SELECT mi.collection_id, mi.index_id,
         documentdb_api_internal.index_spec_as_bson(mi.index_spec),
         mi.index_is_valid
  FROM documentdb_api_catalog.collection_indexes AS mi
  WHERE mi.collection_id = (SELECT mc.collection_id FROM documentdb_api_catalog.collections AS mc
                            WHERE collection_name = p_collection_name AND
                                  database_name = p_database_name);
END;
$$ LANGUAGE plpgsql;

-- query pg_index for the documents table backing given collection
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.get_data_table_indexes (
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

-- count collection indexes grouping by "pg_index.indisprimary" attr
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.count_collection_indexes(
    p_database_name text,
    p_collection_name text)
RETURNS TABLE (
  index_type_is_primary boolean,
  index_type_count bigint
)
AS $$
BEGIN
  RETURN QUERY
  SELECT indisprimary, COUNT(*) FROM pg_index
  WHERE indrelid = (SELECT ('documentdb_data.documents_' || collection_id::text)::regclass
                    FROM documentdb_api_catalog.collections
                    WHERE database_name = p_database_name AND
                          collection_name = p_collection_name)
  GROUP BY indisprimary;
END;
$$ LANGUAGE plpgsql;

-- function to mask variable plan id from the explain output of a distributed subplan
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.mask_plan_id_from_distributed_subplan(explain_command text, out query_plan text)
RETURNS SETOF TEXT AS $$
BEGIN
  FOR query_plan IN EXECUTE explain_command LOOP
    IF query_plan ILIKE '%Distributed Subplan %_%'
    THEN
      RETURN QUERY SELECT REGEXP_REPLACE(query_plan,'[[:digit:]]+','X', 'g');
    ELSE
      RETURN next;
    END IF;
  END LOOP;
  RETURN;
END; $$ language plpgsql;


CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.drop_primary_key(p_database_name text, p_collection_name text)
RETURNS void
AS $$
DECLARE
    v_collection_id bigint;
BEGIN
    SELECT collection_id INTO v_collection_id FROM documentdb_api_catalog.collections WHERE database_name = p_database_name AND collection_name = p_collection_name;
    DELETE FROM documentdb_api_catalog.collection_indexes
    WHERE (index_spec).index_key operator(documentdb_core.=) '{"_id": 1}' AND
          collection_id = v_collection_id;
	EXECUTE format('ALTER TABLE documentdb_data.documents_%s DROP CONSTRAINT collection_pk_%s', v_collection_id, v_collection_id);
END;
$$ LANGUAGE plpgsql;

-- Function to avoid flakiness of a SQL query typically on a sharded multi-node collection. 
-- One way to fix such falkiness is to add an order by clause to inject determinism, but 
-- many queryies like cursors don't support order by. This test function bridges that gap 
-- by storing the result of such queries in a TEMP table and then ordering the entries in the 
-- temp table. One caveat is that the sql query in the argument is expacted to have exact two
-- columns object_id, and document. This seems to be sufficient for now for our use cases.
-- If the caller wants to project multiple columns, thaey can be concatenated as aliased as 'document'
CREATE OR REPLACE FUNCTION execute_and_sort(p_sql TEXT)
RETURNS TABLE (document text) AS $$
BEGIN
    EXECUTE 'CREATE TEMP TABLE temp_dynamic_results ON COMMIT DROP AS ' || p_sql;
    RETURN QUERY EXECUTE 'SELECT document FROM temp_dynamic_results ORDER BY object_id';
    EXECUTE 'DROP TABLE temp_dynamic_results';
END;
$$ LANGUAGE plpgsql;

-- This method mimics how the 2d index extract the geometries from `p_document` from `p_keyPath`
-- This function expects the geospatial data in form of legacy coordinate pairs (longitude, latitude).
-- returns the 2d flat geometry in form of public.geometry.
--
-- This function does strict validation of the values at path for geometry formats and
-- checks for valid points and multipoints input format and throws
-- error if not valid and only is applicable for creating the geospatial index and control
-- insert behaviors for invalid geodetic data points.
--
-- example scenario with native mongo db:
-- - db.coll.createIndex({loc: "2dsphere"});
-- 
-- - db.insert({loc: [10, 'text']}); => This throws error
-- 
-- - db.insert({non-loc: [10, 'text']}) => This is normal insert as no 2d index
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.bson_extract_geometry(
    p_document documentdb_core.bson,
    p_keyPath text)
 RETURNS public.geometry
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT 
AS 'pg_documentdb', $function$bson_extract_geometry$function$;


-- This method mimics how the runtime extract the geometries from `p_document` from `p_keyPath`
-- This is similar to bson_extract_geometry function but
-- it performs a `weak` validation and doesn't throw error in case where the `bson_extract_geometry` function may throw error
-- e.g. scenarios with native mongo db:
-- - db.coll.insert({loc: [[10, 20], [30, 40], ["invalid"]]}); (without 2d index on 'loc')
-- - db.coll.find({loc: {$geoWithin: { $box: [[30, 30], [40, 40]] }}})
--
-- The above find should match the object if any of the point (in multikey point case) matches the
-- geospatial query.
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.bson_extract_geometry_runtime(
    p_document documentdb_core.bson,
    p_keyPath text)
 RETURNS public.geometry
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT
AS 'pg_documentdb', $function$bson_extract_geometry_runtime$function$;

-- This is a helper for create_indexes_background. It performs the submission of index requests in background and wait for their completion.
CREATE OR REPLACE PROCEDURE documentdb_distributed_test_helpers.create_indexes_background(IN p_database_name text, 
                                                        IN p_index_spec documentdb_core.bson,
                                                        IN p_log_index_queue boolean DEFAULT false,
                                                        INOUT retVal documentdb_core.bson DEFAULT null,
                                                        INOUT ok boolean DEFAULT false)
AS $procedure$
DECLARE
  create_index_response record;
  check_build_index_status record;
  completed boolean := false;
  indexRequest text;
  index_cmd_stored text;
BEGIN
  SET search_path TO documentdb_core,documentdb_api;
  SELECT * INTO create_index_response FROM documentdb_api.create_indexes_background(p_database_name, p_index_spec);
  IF p_log_index_queue THEN
    SELECT string_agg(index_cmd, ',') into index_cmd_stored FROM documentdb_api_catalog.documentdb_index_queue;
    RAISE INFO 'Index Queue Commands: %', index_cmd_stored;
  END IF;
  COMMIT;

  IF create_index_response.ok THEN
    SELECT create_index_response.requests->>'indexRequest' INTO indexRequest;
    IF indexRequest IS NOT NULL THEN
      LOOP
          SELECT * INTO check_build_index_status FROM documentdb_api_internal.check_build_index_status(create_index_response.requests);
          IF check_build_index_status.ok THEN 
            completed := check_build_index_status.complete;
            IF completed THEN
              ok := create_index_response.ok;
              retVal := create_index_response.retval;
              RETURN;
            END IF;
          ELSE
            ok := check_build_index_status.ok;
            retVal := check_build_index_status.retval;
            RETURN;
          END IF;
          
          COMMIT; -- COMMIT so that CREATE INDEX CONCURRENTLY does not wait for documentdb_distributed_test_helpers.create_indexes_background
          PERFORM pg_sleep_for('100 ms');
      END LOOP;
    ELSE
      ok := create_index_response.ok;
      retVal := create_index_response.retval;
      RETURN;
    END IF;
  ELSE
    ok := create_index_response.ok;
    retVal := create_index_response.retval;
  END IF;
END;
$procedure$
LANGUAGE plpgsql;


-- This is a helper function to evaluate expressions for testing purposes.
-- This is used by backend tests to validate functionality of comparisons.
CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.evaluate_query_expression(expression documentdb_core.bson, value documentdb_core.bson)
 RETURNS bool
 LANGUAGE c
 IMMUTABLE STRICT
AS '$libdir/pg_documentdb.so', $function$command_evaluate_query_expression$function$;

CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.evaluate_expression_get_first_match(expression documentdb_core.bson, value documentdb_core.bson)
 RETURNS documentdb_core.bson
 LANGUAGE c
 IMMUTABLE STRICT
AS '$libdir/pg_documentdb.so', $function$command_evaluate_expression_get_first_match$function$;

-- validate background worker is launched
SELECT application_name FROM pg_stat_activity WHERE application_name = 'documentdb_bg_worker_leader';

-- create a single table in the 'db' database so that existing tests don't change behavior (yet)
set documentdb.enableNativeColocation to off;
SELECT documentdb_api.create_collection('db', 'firstCollection');
set documentdb.enableNativeColocation to on;
SELECT documentdb_api.create_collection('db', 'secondCollection');

CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.gin_bson_get_single_path_generated_terms(
    document documentdb_core.bson,
    path text,
    isWildcard bool,
    generateNotFoundTerm bool default false,
    addMetadata bool default false,
    indexTermSizeLimit int default -1)
 RETURNS SETOF documentdb_core.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'pg_documentdb', $$gin_bson_get_single_path_generated_terms$$;

CREATE OR REPLACE FUNCTION documentdb_distributed_test_helpers.gin_bson_get_wildcard_project_generated_terms(
    document documentdb_core.bson,
    pathSpec text,
    isExclusion bool,
    includeId bool,
    addMetadata bool default false,
    indexTermSizeLimit int default -1)
 RETURNS SETOF documentdb_core.bson
 LANGUAGE c
 IMMUTABLE PARALLEL SAFE STRICT ROWS 100
AS 'pg_documentdb', $$gin_bson_get_wildcard_project_generated_terms$$;

CREATE FUNCTION documentdb_distributed_test_helpers.get_feature_counter_pretty(p_reset_counter bool)
RETURNS SETOF json
AS
$$
BEGIN
    RETURN QUERY
    SELECT row_to_json(result) FROM ( 
        SELECT coalesce(json_agg(json_build_object(feature_name, usage_count)), '[]'::json) AS "Feature_usage" 
        FROM documentdb_api_internal.command_feature_counter_stats(p_reset_counter)
    ) result;
END;
$$
LANGUAGE plpgsql;