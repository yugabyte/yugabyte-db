CREATE EXTENSION postgres_fdw;

--
-- Test to validate behavior of 'server_type' option in foreign server.
--
CREATE SERVER s_yugabytedb FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'yugabytedb');
CREATE SERVER s_postgres FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'postgresql');
CREATE SERVER s_yugabytedb2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (dbname 'yugabyte', server_type 'yugabytedb');
CREATE SERVER s_nosrvtype FOREIGN DATA WRAPPER postgres_fdw;
CREATE SERVER s_nosrvtype2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (dbname 'yugabyte', host '127.0.0.1');
CREATE SERVER s_invalidsrvtype FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'something_invalid');
CREATE SERVER s_invalidsrvtype2 FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type '');

SELECT srv.srvname, srv.srvoptions
FROM pg_foreign_server srv JOIN pg_foreign_data_wrapper fdw ON srv.srvfdw = fdw.oid
WHERE fdw.fdwname = 'postgres_fdw';

-- Adding a 'server_type' option where none exists.
ALTER SERVER s_nosrvtype2 OPTIONS (ADD server_type 'yugabytedb');
-- Adding a 'server_type' option where one already exists should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (ADD server_type 'postgresql');
-- Modifying 'server_type' option where one already exists should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (SET server_type 'postgresql');
-- Modifying 'server_type' option where one already exists but with an invalid value should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (SET server_type 'something_invalid');
-- Dropping 'server_type' option should be disallowed.
ALTER SERVER s_nosrvtype2 OPTIONS (DROP server_type);
-- Modifying or dropping 'server_type' option where one does not exist should be disallowed.
ALTER SERVER s_nosrvtype OPTIONS (SET server_type 'postgresql');
ALTER SERVER s_nosrvtype OPTIONS (DROP server_type);

SELECT srv.srvname, srv.srvoptions
FROM pg_foreign_server srv JOIN pg_foreign_data_wrapper fdw ON srv.srvfdw = fdw.oid
WHERE fdw.fdwname = 'postgres_fdw';

DROP SERVER s_yugabytedb2;
DROP SERVER s_nosrvtype2;

-- Test to validate that 'server_type' option is not applicable to foreign table.
CREATE FOREIGN TABLE ft_test (v INT) SERVER s_yugabytedb OPTIONS (table_name 't_test', server_type 'yugabytedb');

CREATE TABLE t_test (v INT);
INSERT INTO t_test (SELECT i FROM generate_series(1, 10) AS i);

-- Sanity test to assert that the server has indeed been configured.
DO $d$
    BEGIN
        EXECUTE $$ALTER SERVER s_yugabytedb
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
        EXECUTE $$ALTER SERVER s_postgres
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
        EXECUTE $$ALTER SERVER s_nosrvtype
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
    END;
$d$;

CREATE USER MAPPING FOR CURRENT_USER SERVER s_yugabytedb;
CREATE USER MAPPING FOR CURRENT_USER SERVER s_postgres;
CREATE USER MAPPING FOR CURRENT_USER SERVER s_nosrvtype;

CREATE FOREIGN TABLE ft_test1 (v INT) SERVER s_yugabytedb OPTIONS (table_name 't_test');
SELECT * FROM ft_test1 ORDER BY v;

CREATE FOREIGN TABLE ft_test2 (v INT) SERVER s_postgres OPTIONS (table_name 't_test');
SELECT * FROM ft_test2 ORDER BY v;

CREATE FOREIGN TABLE ft_test3 (v INT) SERVER s_nosrvtype OPTIONS (table_name 't_test');
SELECT * FROM ft_test3 ORDER BY v;

-- A join involving tables of different server types must not be pushed down.
EXPLAIN (VERBOSE, COSTS OFF) SELECT * FROM ft_test1, ft_test2, ft_test3 WHERE ft_test1.v = ft_test2.v AND ft_test1.v = ft_test3.v;
SELECT * FROM ft_test1, ft_test2, ft_test3 WHERE ft_test1.v = ft_test2.v AND ft_test1.v = ft_test3.v;

-- Modifying table level options should be allowed.
ALTER FOREIGN TABLE ft_test1 OPTIONS (SET table_name 't_othertest');
ALTER FOREIGN TABLE ft_test2 OPTIONS (DROP table_name, ADD batch_size '1000');
SELECT * FROM pg_foreign_table;
-- Setting an option should be disallowed if it doesn't already exist.
ALTER FOREIGN TABLE ft_test3 OPTIONS (SET batch_size '1000', SET table_name 't_othertest');
-- Dropping an option should be disallowed if it doesn't already exist.
ALTER FOREIGN TABLE ft_test2 OPTIONS (DROP table_name);
-- Adding an option should be disallowed if it already exists.
ALTER FOREIGN TABLE ft_test1 OPTIONS (ADD table_name 't_test');
SELECT * FROM pg_foreign_table;

-- Restore the original table name.
ALTER FOREIGN TABLE ft_test1 OPTIONS (SET table_name 't_test');
ALTER FOREIGN TABLE ft_test2 OPTIONS (ADD table_name 't_test', DROP batch_size);
SELECT * FROM pg_foreign_table;

DROP FOREIGN TABLE ft_test1;
DROP FOREIGN TABLE ft_test2;
DROP FOREIGN TABLE ft_test3;

--
-- Test to validate behavior of DML queries on foreign tables
--
CREATE TABLE t_simple(k INT, v INT, PRIMARY KEY (k ASC));
CREATE TABLE t_other (k INT, v INT, PRIMARY KEY (k ASC));

CREATE FOREIGN TABLE ft_simple (k INT, v INT) SERVER s_yugabytedb OPTIONS (table_name 't_simple');
CREATE FOREIGN TABLE ft_other (k INT, v INT) SERVER s_yugabytedb OPTIONS (table_name 't_other');

-- Test various types of inserts.
-- In all DMLs with a RETURNING clause, the ybctid column must be requested either
-- explicitly or implicitly.
EXPLAIN (VERBOSE, COSTS OFF) INSERT INTO ft_simple VALUES (1, 1) RETURNING k, v;
INSERT INTO ft_simple VALUES (0, 0);
INSERT INTO ft_simple VALUES (1, 1) RETURNING k, v;
INSERT INTO ft_simple VALUES (2, 2) RETURNING *;
INSERT INTO ft_simple (v, k) VALUES (3, 3) RETURNING tableoid::regclass, *;
INSERT INTO ft_simple VALUES (4, 4) RETURNING ybctid, *;

SELECT * FROM ft_simple ORDER BY k;

-- Test "direct" updates and deletes. These are operations that can be pushed down
-- to the foreign server.
EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple SET k = k + 10, v = v + 1 WHERE k = 1;
EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple SET v = v + 1 WHERE k < 10 RETURNING k, v;
UPDATE ft_simple SET v = v + 1 WHERE k = 0;
UPDATE ft_simple SET k = k + 10, v = v + 1 WHERE k = 1 RETURNING k, v;
UPDATE ft_simple SET v = v + 1 WHERE k < 10 RETURNING *;
UPDATE ft_simple SET k = k + 10 WHERE v = 3 RETURNING ybctid, *;

SELECT * FROM ft_simple ORDER BY k;

EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple WHERE k = 0;
EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple WHERE k < 10 RETURNING k, v;
DELETE FROM ft_simple WHERE k = 0;
DELETE FROM ft_simple WHERE k < 10 RETURNING k, v;
DELETE FROM ft_simple WHERE k = 11 RETURNING *;
DELETE FROM ft_simple WHERE v = 3 RETURNING ybctid, *;

SELECT * FROM ft_simple ORDER BY k;

-- Test "two-pass" updates and deletes. These are operations that cannot be pushed down
-- to the foreign server, and require a foreign scan to return the ctid (ybctid) of
-- the row being deleted.
-- Create a "local" function. postgres_fdw will not push down function calls
-- because it cannot determine if the same implementation of the function exists
-- on the foreign server.
CREATE FUNCTION fdw(a INT) RETURNS INT
LANGUAGE PLPGSQL AS $$
BEGIN
	RETURN a;
END;
$$;

INSERT INTO t_simple (SELECT i, i FROM generate_series(1, 10) AS i);

EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple SET v = fdw(v) WHERE k = 1;
EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple SET v = v + 1 WHERE fdw(k) = 1 RETURNING *;
EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple WHERE fdw(k) = 1 RETURNING *;
-- This query should be capable of push down.
EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple WHERE k = 1 RETURNING fdw(k), fdw(v);

UPDATE ft_simple SET v = v + 1 WHERE fdw(k) = 1;
UPDATE ft_simple SET k = fdw(k) + 10, v = fdw(v) + 1 WHERE k = 2 RETURNING k, v;
UPDATE ft_simple SET v = fdw(v) + 1 WHERE fdw(k) < 10 RETURNING *;

SELECT * FROM ft_simple ORDER BY k;

DELETE FROM ft_simple WHERE fdw(k) = 1;
DELETE FROM ft_simple WHERE fdw(k) = 12 RETURNING k, v;
DELETE FROM ft_simple WHERE fdw(v) < 10 RETURNING *;

SELECT * FROM ft_simple ORDER BY k;

INSERT INTO t_simple (SELECT i, i FROM generate_series(1, 5) AS i);
INSERT INTO t_other (SELECT i, i FROM generate_series(1, 5) AS i);

-- Updates and deletes involving multiple foreign tables cannot be pushed down
-- when the conditions are non-trivial.
EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple s SET v = s.v + 1
FROM ft_other t
WHERE s.k < 2 AND t.v = s.v;
EXPLAIN (VERBOSE, COSTS OFF) UPDATE ft_simple s SET v = s.v + 1
FROM ft_other t1 JOIN ft_other t2 ON t1.k = t2.k
WHERE s.k = 2 AND t1.v = s.v;
EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple s
USING ft_other t
WHERE s.k < 2 AND t.v = s.v;
EXPLAIN (VERBOSE, COSTS OFF) DELETE FROM ft_simple s
USING ft_other t1 JOIN ft_other t2 ON t1.k = t2.k
WHERE s.k = 2 AND t1.v = s.v;

UPDATE ft_simple s SET v = s.v + 1
FROM ft_other t
WHERE s.k < 2 AND t.v = s.v
RETURNING *;
UPDATE ft_simple s SET v = s.v + 1
FROM ft_other t1 JOIN ft_other t2 ON t1.k = t2.k
WHERE s.k = 2 AND t1.v = s.v
RETURNING *;
DELETE FROM ft_simple s
USING ft_other t
WHERE s.k < 2 AND t.v = s.v
RETURNING *;
DELETE FROM ft_simple s
USING ft_other t1 JOIN ft_other t2 ON t1.k = t2.k
WHERE s.k = 2 AND t1.v = s.v
RETURNING *;

SELECT * FROM ft_simple ORDER BY k;

CREATE SERVER IF NOT EXISTS gv_server FOREIGN DATA WRAPPER postgres_fdw OPTIONS (server_type 'federatedYugabyteDB');

CREATE FOREIGN TABLE IF NOT EXISTS "gv$yb_active_session_history" (
    sample_time TIMESTAMPTZ,
    root_request_id UUID,
    rpc_request_id BIGINT,
    wait_event_component TEXT,
    wait_event_class TEXT,
    wait_event TEXT,
    top_level_node_id UUID,
    query_id BIGINT,
    pid INT,
    client_node_ip TEXT,
    wait_event_aux TEXT,
    sample_weight REAL,
    wait_event_type TEXT,
    ysql_dbid OID,
    wait_event_code BIGINT,
    pss_mem_bytes BIGINT,
    ysql_userid OID,
    plan_id BIGINT
)
SERVER gv_server
OPTIONS (schema_name 'pg_catalog', table_name 'yb_active_session_history');

SELECT * FROM gv$yb_active_session_history;
SET yb_enable_global_views = TRUE;
SELECT * FROM gv$yb_active_session_history;
SET yb_enable_global_views = FALSE;
