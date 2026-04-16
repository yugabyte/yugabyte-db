SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 4900000;
SET documentdb.next_collection_id TO 4900;
SET documentdb.next_collection_index_id TO 4900;

CREATE OR REPLACE FUNCTION generate_random_values(p_doc bson, p_rand_spec bson)
RETURNS bool
AS $$
DECLARE
    random_number numeric;
BEGIN
    FOR cnt in 1..3000
    LOOP
        SELECT (bson_expression_get(p_doc, p_rand_spec)->>'field')::float8
        INTO random_number;
        IF random_number < 0 AND random_number >= 1 THEN
		    RAISE NOTICE '$rand generated out of range double value';
            RETURN false;
	    END IF;
    END LOOP;
    RETURN true;
END
$$
LANGUAGE plpgsql;

-- $rand error cases where the value of $rand non - object / non empty
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": 5 } }');
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": false } }');
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": [5] } }');
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": {"value": 5} } }');

-- $rand valid empty params
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": {} } }');
SELECT generate_random_values('{"a": 1}', '{ "b" : { "$rand": [] } }');
