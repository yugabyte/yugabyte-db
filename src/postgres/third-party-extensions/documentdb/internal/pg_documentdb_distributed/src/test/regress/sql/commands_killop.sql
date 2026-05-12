SET documentdb.next_collection_id TO 1583000;
SET documentdb.next_collection_index_id TO 1583000;
SET citus.next_shard_id TO 15830000;
SET search_path TO documentdb_api, documentdb_api_catalog,documentdb_core;


-- test some negative cases for killOp
-- Valid op field is an id and is a combination of <shardid>:<operationId>
SELECT documentdb_api.kill_op(NULL);
SELECT documentdb_api.kill_op('{}');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "12345" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "12345:" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": ":" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": ":12345" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "foo:baar" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "12345:1234C5" }');
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "10000000001:12345" }'); -- No databse specified
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "10000000001:12345", "$db": "test" }'); -- Valid but not against admin db

-- Valid but no-ops
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "10000004122:12345", "$db": "admin" }');

-- Validate a random user can't run kill_op without pg_signal_backend role
CREATE ROLE killop_user WITH LOGIN;
GRANT ALL PRIVILEGES ON SCHEMA documentdb_api TO killop_user;
SET SESSION AUTHORIZATION killop_user;

-- This should fail with unauthorized error
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "10000004122:12345", "$db": "admin" }');
RESET SESSION AUTHORIZATION;

-- Now grant pg_signal_backend role and try again
GRANT pg_signal_backend TO killop_user;
SET SESSION AUTHORIZATION killop_user;
-- This should succeed now
SELECT documentdb_api.kill_op('{ "killOp": 1, "op": "10000004122:12345", "$db": "admin" }');
RESET SESSION AUTHORIZATION;

-- Cleanup
REVOKE ALL PRIVILEGES ON SCHEMA documentdb_api FROM killop_user;
DROP ROLE killop_user;