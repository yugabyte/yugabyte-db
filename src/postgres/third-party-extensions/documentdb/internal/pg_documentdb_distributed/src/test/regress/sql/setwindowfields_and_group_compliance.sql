SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 884500;
SET documentdb.next_collection_id TO 88450;
SET documentdb.next_collection_index_id TO 88450;

CREATE SCHEMA setWindowFieldSchema;

-- The test is intended to set an agreement between the setwindowfields and group aggregate operators
-- to be working on both cases (if implemented)
-- [Important] - When any operator is supported for both $group and $setWindowFields then please remove it from here.
CREATE OR REPLACE FUNCTION get_unsupported_operators()
RETURNS TABLE(unsupported_operator text) AS $$
BEGIN
    RETURN QUERY SELECT unnest(ARRAY[
        '$bottom',
        '$bottomN',
        '$maxN',
        '$median',
        '$minN',
        '$percentile',
        '$stdDevSamp',
        '$stdDevPop',
        '$top',
        '$topN'
    ]) AS unsupported_operator;
END;
$$ LANGUAGE plpgsql;


-- Make a non-empty collection
SELECT documentdb_api.insert_one('db','setWindowField_compliance','{ "_id": 1 }', NULL);

DO $$
DECLARE
    operator text;
    errorMessage text;
    supportedInSetWindowFields BOOLEAN;
    supportedInGroup BOOLEAN;
    querySpec bson;
BEGIN
    FOR operator IN SELECT * FROM get_unsupported_operators()
    LOOP
        supportedInSetWindowFields := TRUE;
        supportedInGroup := TRUE;
        querySpec := FORMAT('{ "aggregate": "setWindowField_compliance", "pipeline":  [{"$group": { "_id": "$_id", "test": {"%s": { } } }}]}', operator)::documentdb_core.bson;
        BEGIN
            SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', querySpec);
        EXCEPTION WHEN OTHERS THEN
            errorMessage := SQLERRM;
            IF errorMessage LIKE '%Unknown group operator%' OR errorMessage LIKE '%not implemented%' THEN
                supportedInGroup := FALSE;
            END IF;
        END;

        querySpec := FORMAT('{ "aggregate": "setWindowField_compliance", "pipeline":  [{"$setWindowFields": { "output": { "field": { "%s": { } } } }}]}', operator)::documentdb_core.bson;
        BEGIN
            SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', querySpec);
        EXCEPTION WHEN OTHERS THEN
            errorMessage := SQLERRM;
            IF errorMessage LIKE '%not supported%' THEN
                supportedInSetWindowFields := FALSE;
            END IF;
        END;

        IF supportedInSetWindowFields <> supportedInGroup THEN
            RAISE NOTICE '[TEST FAILED] Operator % is not supported in %', operator, CASE WHEN supportedInSetWindowFields THEN '$group' ELSE '$setWindowFields' END;
        ELSEIF supportedInSetWindowFields THEN
            RAISE NOTICE '[TEST PASSED] Operator % is supported in both $group and $setWindowFields', operator;
        ELSE
            RAISE NOTICE '[TEST PASSED] Operator % is not supported in both $group and $setWindowFields', operator;
        END IF;
    END LOOP;
END $$;

DROP SCHEMA setWindowFieldSchema CASCADE;

CREATE OR REPLACE FUNCTION check_aggregates(num_elements int)
RETURNS void AS $fn$
DECLARE
    bson_spec jsonb;
    bson_spec_str text;
    group_by_spec text;
    setwindowFields_spec text;
    groupResult bson;
    setwindowFieldsResult bson;
    groupExplainResult jsonb;
    setwindowFieldsExplainResult jsonb;
    i int;
BEGIN
    -- Initialize the BSON spec as an empty JSONB object
    bson_spec := '{}'::jsonb;

    -- Loop to add 50 fields to the BSON spec
    FOR i IN 1..num_elements LOOP
        bson_spec := jsonb_set(bson_spec, ARRAY['field' || i::text], jsonb_build_object('$sum', 1));
    END LOOP;

    -- Output the BSON spec
    bson_spec_str := bson_spec::text;
    RAISE NOTICE '\n============= TESTING MULTIPLE ARGUMENTS AGGREGATES (%) =======================' , num_elements;

    group_by_spec := '{ "aggregate": "setWindowField_compliance", "pipeline":  [{"$group": { "_id": "$_id", ' ||  substring(bson_spec_str, 2, length(bson_spec_str) - 2)  || ' }}]}';
    setwindowFields_spec := '{ "aggregate": "setWindowField_compliance", "pipeline":  [{"$setWindowFields": { "output": ' ||  bson_spec_str || ' }}]}';

    SET citus.log_remote_commands = 'on';
    SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', group_by_spec::documentdb_core.bson) INTO groupResult;
    RAISE NOTICE E'\n=============\nGroup by\n=============\nQuery: %\n\nResult: %', group_by_spec, groupResult;

    SELECT document FROM documentdb_api_catalog.bson_aggregation_pipeline('db', setwindowFields_spec::documentdb_core.bson) INTO setwindowFieldsResult;
    RAISE NOTICE E'\n=============\nSetWindowFields\n=============\nQuery: %\n\nResult: %', setwindowFields_spec, setwindowFieldsResult;
    RESET citus.log_remote_commands;
END;
$fn$ LANGUAGE plpgsql;

-- Check aggregates with different number of aggregation operators
SELECT check_aggregates(30);
SELECT check_aggregates(50);
SELECT check_aggregates(51);
SELECT check_aggregates(100);
SELECT check_aggregates(105);
SELECT check_aggregates(200);
