SET search_path TO documentdb_core;
set documentdb_core.bsonUseEJson to on;

-- numerics
WITH c1 AS (SELECT 1::numeric AS "col1") SELECT row_get_bson(c1) FROM c1;
WITH c1 AS (SELECT '1.1'::numeric AS "col1") SELECT row_get_bson(c1) FROM c1;
WITH c1 AS (SELECT '1235313413431343'::numeric AS "col1") SELECT row_get_bson(c1) FROM c1;
WITH c1 AS (SELECT '1.1e600'::numeric AS "col1") SELECT row_get_bson(c1) FROM c1;

-- strings
WITH c1 AS (SELECT 'string value' AS "col1") SELECT row_get_bson(c1) FROM c1;