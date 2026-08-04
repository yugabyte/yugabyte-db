SET search_path TO documentdb_api_catalog, postgis_public;
SET citus.next_shard_id TO 168300;
SET documentdb.next_collection_id TO 16830;
SET documentdb.next_collection_index_id TO 16830;


\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT documentdb_api.drop_collection('db', 'geoquerytest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'geoquerytest') IS NOT NULL;
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"a.b": "2d"}, "name": "my_2d_a_idx" }]}', true); -- 2d index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"c.d": "2d"}, "name": "my_2d_c_idx" }]}', true); -- 2d index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo.loc": "2dsphere"}, "name": "my_2ds_geoloc_idx" }]}', true); -- 2dsphere index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geoA": "2dsphere", "geoB": "2dsphere"}, "name": "my_2dsgeo_a_or_b_idx" }]}', true); -- Composite 2dsphere index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo1": "2dsphere", "geo2": "2dsphere"}, "name": "my_2ds_geo1_2_pfe_idx", "partialFilterExpression": { "region": { "$eq": "USA" } } }]}', true); -- Multi 2dsphere index with pfe

-- Also create a regular index on the same path, to validate that geo operators are never pushed to regular index
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"a.b": 1}, "name": "my_regular_a_idx" }]}', true);
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{"createIndexes": "geoquerytest", "indexes": [{"key": {"geo.loc": 1}, "name": "my_regular_geoloc_idx" }]}', true);

\o
\set ECHO :prevEcho

-- Tests without sharding

-- Geometries
BEGIN;
set local enable_seqscan TO off;
\i sql/bson_query_operator_geospatial_core.sql
ROLLBACK;

-- Geographies
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_geography_core.sql
ROLLBACK;

-- Again testing with shards
-- Shard the collection and run the tests
SELECT documentdb_api.shard_collection('db', 'geoquerytest', '{ "_id": "hashed" }', false);

-- Geometries
BEGIN;
set local enable_seqscan TO off;
\i sql/bson_query_operator_geospatial_core.sql
ROLLBACK;

-- Geographies
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_geography_core.sql
ROLLBACK;
