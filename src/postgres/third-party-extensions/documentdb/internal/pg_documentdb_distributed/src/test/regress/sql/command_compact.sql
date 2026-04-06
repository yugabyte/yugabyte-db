SET citus.next_shard_id TO 2432000;
SET documentdb.next_collection_id TO 24320;
SET documentdb.next_collection_index_id TO 24320;
SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;

SELECT documentdb_api.compact(NULL);
SELECT documentdb_api.compact('{}');
SELECT documentdb_api.compact('{"noIdea": "collection1"}');
SELECT documentdb_api.compact('{"compact": "non_existing_collection"}');
SELECT documentdb_api.compact('{"compact": 1 }');
SELECT documentdb_api.compact('{"compact": true }');
SELECT documentdb_api.compact('{"compact": ["coll"]}');

SELECT documentdb_api.create_collection('commands_compact_db','compact_test');

SELECT documentdb_api.compact('{"compact": "compact_test", "dryRun": "invalid"}');
SELECT documentdb_api.compact('{"compact": "compact_test", "comment": "test comment"}');
SELECT documentdb_api.compact('{"compact": "compact_test", "force": false}');

-- Insert a single document
SELECT documentdb_api.insert_one('commands_compact_db', 'compact_test', FORMAT('{ "_id": 1, "a": "%s", "c": [ %s "d" ] }', repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);


-- Bloat the table
DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..45 LOOP
UPDATE documentdb_data.documents_24321 SET document = document::bytea::bson WHERE document @@ '{ "_id": 1 }';
COMMIT;
END LOOP;
END;
$$;

-- VALID, first need to analyze the table so that stats are up to date
ANALYZE VERBOSE documentdb_data.documents_24321;
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test')->>'storageSize';

-- TODO: even after analyze it seems like pg_class stats for toast tables are not updated for test run.
-- Need to investigate this further for test, for live servers this should be okay because the analyze thereshold is set to 0.
SELECT documentdb_api.compact('{"compact": "compact_test", "$db": "commands_compact_db", "dryRun": true}');
SELECT documentdb_api.compact('{"compact": "compact_test", "$db": "commands_compact_db", "dryRun": false}');
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test')->>'storageSize';

SELECT documentdb_api.drop_collection('commands_compact_db','compact_test');

-- sharded test
SELECT documentdb_api.create_collection('commands_compact_db', 'compact_test_sharded');
SELECT documentdb_api.shard_collection('commands_compact_db', 'compact_test_sharded', '{"_id": "hashed"}', false);

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..51 LOOP
PERFORM documentdb_api.insert_one('commands_compact_db', 'compact_test_sharded', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',i, repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
COMMIT;
END LOOP;
END;
$$;

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..45 LOOP
UPDATE documentdb_data.documents_24322 SET document = document::bytea::bson WHERE document @@ '{ "_id": 1 }';
COMMIT;
END LOOP;
END;
$$;

-- VALID, first need to analyze the table so that stats are up to date
ANALYZE VERBOSE documentdb_data.documents_24322;
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test_sharded')->>'storageSize';

-- TODO: even after analyze it seems like pg_class stats for toast tables are not updated for test run.
-- Need to investigate this further for test, for live servers this should be okay because the analyze thereshold is set to 0.
SELECT documentdb_api.compact('{"compact": "compact_test_sharded", "$db": "commands_compact_db", "dryRun": true}');
SELECT documentdb_api.compact('{"compact": "compact_test_sharded", "$db": "commands_compact_db", "dryRun": false}');
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test_sharded')->>'storageSize';

SELECT documentdb_api.drop_collection('commands_compact_db','compact_test_sharded');


-- sharded test
SELECT documentdb_api.create_collection('commands_compact_db', 'compact_test_sharded');
SELECT documentdb_api.shard_collection('commands_compact_db', 'compact_test_sharded', '{"_id": "hashed"}', false);

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..51 LOOP
PERFORM documentdb_api.insert_one('commands_compact_db', 'compact_test_sharded', FORMAT('{ "_id": %s, "a": "%s", "c": [ %s "d" ] }',i, repeat('Sample', 100000), repeat('"' || repeat('a', 1000) || '", ', 5000))::documentdb_core.bson);
COMMIT;
END LOOP;
END;
$$;

DO $$
DECLARE i int;
BEGIN
-- each doc is "a": 500KB, "c": 5 MB - ~5.5 MB & there's 10 of them
FOR i IN 1..45 LOOP
UPDATE documentdb_data.documents_24323 SET document = document::bytea::bson WHERE document @@ '{ "_id": 1 }';
COMMIT;
END LOOP;
END;
$$;

SET citus.show_shards_for_app_name_prefixes to '*';

-- VALID, first need to analyze the table so that stats are up to date
ANALYZE VERBOSE documentdb_data.documents_24323;
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test_sharded')->>'storageSize';

-- set relpages to INT32_MAX - 1
UPDATE pg_class
SET relpages = 2147483646
WHERE oid = 'documentdb_data.documents_24323_2432023'::regclass;

SELECT documentdb_api.compact('{"compact": "compact_test_sharded", "$db": "commands_compact_db", "dryRun": true}');
SELECT documentdb_api.compact('{"compact": "compact_test_sharded", "$db": "commands_compact_db", "dryRun": false}');
SELECT documentdb_api.coll_stats('commands_compact_db','compact_test_sharded')->>'storageSize';
RESET citus.show_shards_for_app_name_prefixes;
SELECT documentdb_api.drop_collection('commands_compact_db','compact_test_sharded');