SET yb_fetch_row_limit TO 1024;
SET yb_explain_hide_non_deterministic_fields TO true;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

-- This test requires the t-server preview/auto flag 'ysql_yb_update_optimizations_infra' to be enabled.

-- CREATE functions that can be triggered upon update to modify various columns
CREATE OR REPLACE FUNCTION no_update() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  RAISE NOTICE 'Trigger "no_update" invoked';
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION increment_v1() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.v1 = NEW.v1 + 1;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION increment_v3() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.v3 = NEW.v3 + 1;
  RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION update_all() RETURNS TRIGGER
LANGUAGE PLPGSQL AS $$
BEGIN
  NEW.h = NEW.h + 1024;
  NEW.v1 = NEW.v1 + 1024;
  NEW.v2 = NEW.v2 + 1;
  RETURN NEW;
END;
$$;

-- CREATE a simple table with only a primary key
DROP TABLE IF EXISTS pkey_only_table;
CREATE TABLE pkey_only_table (h INT PRIMARY KEY, v INT);
INSERT INTO pkey_only_table (SELECT i, i FROM generate_series(1, 1024) AS i);

-- A simple point update without involving the primary key
-- This query does not go through the distributed transaction path
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET v = v + 1 WHERE h = 1;

-- Point updates that include the primary key in the targetlist
-- These queries do not go through the distributed transaction path either
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = v - 1, v = v + 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = h, v = v + 1 WHERE h = 1; -- Needs further optimization
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = 1, v = v + 1 WHERE h = 1;

-- Queries affecting a range of rows
-- Since the primary key is not specified in its entirety, these use the distributed txns
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = h, v = v + 1 WHERE h > 10 AND h < 15;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = v, v = v + 1 WHERE h > 20 AND h < 25;

-- Query that updates the primary key. This should involve multiple flushes
-- over a distributed transaction.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = h + 1024, v = v + 1 WHERE h < 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE pkey_only_table SET h = h + 1024, v = v + 1 WHERE h < 2000;

DROP TABLE pkey_only_table;

-- CREATE a table with no primary key, but having a secondary indexes.
DROP TABLE IF EXISTS secindex_only_table;
CREATE TABLE secindex_only_table (v1 INT, v2 INT, v3 INT);
INSERT INTO secindex_only_table (SELECT i, i, i FROM generate_series(1, 1024) AS i);

-- Add an index on v1
CREATE INDEX NONCONCURRENTLY secindex_only_table_v1 ON secindex_only_table (v1);

-- Updates not involving the secondary index should not have multiple flushes.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v3 = v2 + 1 WHERE v1 = 1;
-- This specifically tests the case where there is no overlap between the index
-- and columns that are potentially modified. The index should be added to a
-- skip list without further consideration
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 WHERE v2 = 1;

-- Point updates that include the column referenced by the index in the targetlist.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v2, v3 = v3 + 1 WHERE v1 = 1;

-- Queries affecting a range of rows
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v3 = v3 + 1 WHERE v1 < 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v2, v3 = v3 + 1 WHERE v1 < 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v3 = v3 + 1 WHERE v2 < 5;

-- Special case where no column is affected. We should still see a flush in order
-- to acquire necessary row lock on the main table. This is similar to SELECT FOR UPDATEs.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 WHERE v1 = 5;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 WHERE v1 < 5;

-- Add a second index to the table which is a multi-column index
CREATE INDEX NONCONCURRENTLY secindex_only_table_v1_v2 ON secindex_only_table (v1, v2);

-- Queries that affect only one of the two indexes
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 WHERE v2 = 1;

-- Same as above but queries that actually modify one of the indexes exclusively.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2 + 1, v3 = v3 WHERE v1 = 15;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2 + 1, v3 = v3 WHERE v2 = 16;

-- Queries that cover both indexes.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v3 = v3 WHERE v1 = 15;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v2 = v2 + 1, v3 = v3 WHERE v1 = 15;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 + 1, v2 = v2 + 1, v3 = v3 WHERE v1 = 15;

-- Add a simple trigger to the table.
CREATE TRIGGER secindex_only_table_no_update BEFORE UPDATE ON secindex_only_table FOR EACH ROW EXECUTE FUNCTION no_update();

-- Repeat the above queries to validate that the number of flushes do not change.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 WHERE v2 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2 + 1, v3 = v3 WHERE v1 = 25;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v2 = v2 + 1, v3 = v3 WHERE v1 = 25;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 + 1, v2 = v2 + 1, v3 = v3 WHERE v1 = 25;

-- Add a trigger that modifies v3 (no indexes on it)
CREATE TRIGGER secindex_only_table_increment_v3 BEFORE UPDATE ON secindex_only_table FOR EACH ROW EXECUTE FUNCTION increment_v3();
-- Read the values corresponding to v1 = 1.
SELECT * FROM secindex_only_table WHERE v1 = 1;

-- Repeat a subset of the above queries to validate that no extra flushes are required.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2 + 1, v3 = v3 WHERE v1 = 35;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v2 = v2 + 1, v3 = v3 WHERE v1 = 35;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 + 1, v2 = v2 + 1, v3 = v3 WHERE v1 = 35;

-- Read the values corresponding to v1 = 1 again to validate that v3 is incremented twice:
-- once by the query, once by the trigger.
SELECT * FROM secindex_only_table WHERE v1 = 1;

-- Add a trigger that modifies v1 (has two indexes on it)
CREATE TRIGGER secindex_only_table_increment_v1 BEFORE UPDATE ON secindex_only_table FOR EACH ROW EXECUTE FUNCTION increment_v1();
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2, v3 = v3 WHERE v2 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1, v2 = v2 + 1, v3 = v3 WHERE v1 = 45;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 + 1, v2 = v2 + 1, v3 = v3 WHERE v1 = 55;

-- Read the values to confirm that there is/are:
-- No row corresponding to v1 = 45
-- Two rows for v1 = 46
-- One row for v1 = 47
-- No rows for v1 = 55
-- One row for v1 = 46
-- Two rows for v1 = 47
SELECT * FROM secindex_only_table WHERE v1 IN (45, 46, 47, 55, 56, 57) ORDER BY v1, v2;

-- Query that updates a range of values between 61 and 70.
-- TODO: Validate this
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v2 = v2 + 1, v3 = v3 WHERE v1 > 60 AND v1 <= 70;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE secindex_only_table SET v1 = v1 + 1, v3 = v3 + 1 WHERE v1 > 70 AND v1 <= 80;

DROP TABLE secindex_only_table;

-- CREATE a table with no primary key or secondary indexes.
DROP TABLE IF EXISTS no_index_table;
CREATE TABLE no_index_table (h INT, v INT);
INSERT INTO no_index_table (SELECT i, i FROM generate_series(1, 1024) AS i);

-- Point update queries. Irrespective of whether the columns in the targetlist
-- are modified, we should see flushes updating the row.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_index_table SET h = h, v = v WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_index_table SET h = h WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_index_table SET h = h + 1 WHERE h = 10;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_index_table SET v = v + 1 WHERE v = 10;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE no_index_table SET h = v + 1, v = h + 1 WHERE v = 20;

-- CREATE a hierarchy of tables of the order a <-- b <-- c where 'a' is the base
-- table, 'b' references 'a' and is a reference to 'c', and so on.
DROP TABLE IF EXISTS a_test;
DROP TABLE IF EXISTS b1_test;
DROP TABLE IF EXISTS b2_test;
DROP TABLE IF EXISTS c1_test;
DROP TABLE IF EXISTS c2_test;

CREATE TABLE a_test (h INT PRIMARY KEY, v1 INT UNIQUE, v2 INT);
CREATE INDEX NONCONCURRENTLY a_v1 ON a_test (v1);
CREATE TABLE b1_test (h INT PRIMARY KEY REFERENCES a_test (h), v1 INT UNIQUE REFERENCES a_test (v1), v2 INT);
CREATE INDEX NONCONCURRENTLY b1_v1 ON b1_test (v1);
CREATE TABLE b2_test (h INT PRIMARY KEY REFERENCES a_test (v1), v1 INT REFERENCES a_test (h), v2 INT);
CREATE INDEX NONCONCURRENTLY b2_v1 ON b2_test (v1);

INSERT INTO a_test (SELECT i, i, i FROM generate_series(1, 1024) AS i);
INSERT INTO b1_test (SELECT i, i, i FROM generate_series(1, 1024) AS i);
INSERT INTO b2_test (SELECT i, i, i FROM generate_series(1, 1024) AS i);

-- Point update queries on table 'a'. Should skip 'referenced-by' constraints on
-- b-level tables.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET h = h, v1 = h, v2 = v2 + 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET v1 = v1, v2 = v2 + 1 WHERE h = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET v1 = v1, v2 = v2 + 1 WHERE v1 = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET v1 = v1, v2 = v2 + 1 WHERE v1 = h AND h = 2;

-- Same as above but update the rows to validate 'referenced-by' constraints.
INSERT INTO a_test (SELECT i, i, i FROM generate_series(1025, 1034) AS i);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET h = h, v1 = v1 + 1 WHERE h = 1034;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE a_test SET h = h + 1, v1 = v1 + 1 WHERE h = 1034;

--
-- The following sections tests updates when the update optimization is turned
-- off. This is to prevent regressions.
--
SET yb_update_num_cols_to_compare TO 0;
DROP TABLE IF EXISTS base_table1;
CREATE TABLE base_table1 (k INT PRIMARY KEY, v1 INT, v2 INT);
INSERT INTO base_table1 (SELECT i, i, i FROM generate_series(1, 10) AS i);

-- This query below should follow the transaction fast path.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 + 1 WHERE k = 1;

-- The queries below should use the distributed transaction path.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = v1 - 1, v1 = v1 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = 1, v2 = v2 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = k, v1 = v1 + 1 WHERE k > 5 AND k < 15;

-- Adding a trigger should automatically make the table use the distributed
-- transaction path.
CREATE TRIGGER base_table1_no_update BEFORE UPDATE ON base_table1 FOR EACH ROW EXECUTE FUNCTION no_update();
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 + 1 WHERE k = 1;
DROP TRIGGER base_table1_no_update ON base_table1;

-- Add a trigger that updates values of v1.
CREATE TRIGGER base_table1_increment_v1 BEFORE UPDATE ON base_table1 FOR EACH ROW EXECUTE FUNCTION increment_v1();
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v2 = v2 + 1 WHERE k = 1;
-- v1 should be updated twice
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 + 1 WHERE k = 1;
DROP TRIGGER base_table1_increment_v1 ON base_table1;

-- Drop the trigger and adding a secondary index should produce the same result.
CREATE INDEX NONCONCURRENTLY base_table1_v1 ON base_table1 (v1);
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = 1, v2 = v2 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 WHERE k = 1;

-- Validate that the values have indeed been updated.
SELECT * FROM base_table1 WHERE k = 1;
-- Use an index-only scan to validate that the index has been updated.
SELECT v1 FROM base_table1 WHERE k = 1;

DROP TABLE base_table1;

-- Turn the GUC back on for the remaining tests
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

--
-- The following section contains a set of sanity tests for colocated tables/databases.
--
CREATE DATABASE codb colocation = true;
\c codb
SET yb_fetch_row_limit TO 1024;
SET yb_explain_hide_non_deterministic_fields TO true;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

DROP TABLE IF EXISTS base_table1;
CREATE TABLE base_table1 (k INT PRIMARY KEY, v1 INT, v2 INT) WITH (colocation = true);
INSERT INTO base_table1 (SELECT i, i, i FROM generate_series(1, 10) AS i);

-- This query below should follow the transaction fast path.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET v1 = v1 + 1 WHERE k = 1;

-- The queries below should use the distributed transaction path.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = v1 - 1, v1 = v1 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = 1, v2 = v2 + 1 WHERE k = 1;
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE base_table1 SET k = k, v1 = v1 + 1 WHERE k > 5 AND k < 15;

DROP TABLE base_table1;

\c yugabyte
SET yb_fetch_row_limit TO 1024;
SET yb_explain_hide_non_deterministic_fields TO true;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

-- The queries below test self-referential foreign keys
DROP TABLE IF EXISTS ancestry;
CREATE TABLE ancestry (key TEXT PRIMARY KEY, value TEXT, parent TEXT, gparent TEXT);
ALTER TABLE ancestry ADD CONSTRAINT fk_self_parent_to_key FOREIGN KEY (parent) REFERENCES ancestry (key);
ALTER TABLE ancestry ADD CONSTRAINT fk_self_gparent_to_key FOREIGN KEY (gparent) REFERENCES ancestry (key);

-- Insert the family tree
INSERT INTO ancestry VALUES ('root', 'Dave', NULL, NULL);
INSERT INTO ancestry VALUES ('ggparent1', 'Adam', 'root', NULL);
INSERT INTO ancestry VALUES ('ggparent2', 'Barry', 'root', NULL);
INSERT INTO ancestry VALUES ('gparent1', 'Claudia', 'ggparent1', 'root');
INSERT INTO ancestry VALUES ('gparent2', 'Donatello', 'ggparent1', 'root');
INSERT INTO ancestry VALUES ('gparent3', 'Eddie', 'ggparent2', 'root');
INSERT INTO ancestry VALUES ('parent1', 'Farooq', 'gparent1', 'ggparent1');
INSERT INTO ancestry VALUES ('parent2', 'Govind', 'gparent1', 'ggparent2');
INSERT INTO ancestry VALUES ('child1', 'Harry', 'parent1', 'gparent1');
INSERT INTO ancestry VALUES ('child2', 'Ivan', 'parent2', 'gparent1');

-- Query modifying the value column should not trigger referential constraint check.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET value = 'David' WHERE value = 'Dave';
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET key = 'root', value = 'David' WHERE key = 'root';

-- Query modifying one's parent should trigger trigger the referential
-- constraint in only one direction to see if the parent exists. Grandparent
-- checks in both directions should be skipped.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET parent = 'ggparent2', gparent = 'root' WHERE value = 'Donatello';
-- This query should throw an error as the parent doesn't exist
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET parent = 'ggparent23', gparent = 'root' WHERE key = 'gparent2';
-- Query modifying the key to the row should trigger checks in only the children
-- and grandchildren.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET key = 'gparent30' WHERE value = 'Eddie';
-- Same query, no modification, no triggers
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET key = 'gparent30' WHERE value = 'Eddie';
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE ancestry SET key = 'gparent3' WHERE key = 'gparent30';

DROP TABLE ancestry;

---
--- The following sections contains sanity test for prepared statements and plan caching
---
CREATE TABLE IF NOT EXISTS t_parent(k INT UNIQUE);
CREATE TABLE IF NOT EXISTS t_simple(k INT PRIMARY KEY, v1 INT REFERENCES t_parent(k), v2 INT, v3 INT, v4 INT);

CREATE INDEX NONCONCURRENTLY t_simple_v1 ON t_simple (v1);
CREATE INDEX NONCONCURRENTLY t_simple_v2 ON t_simple (v2);
INSERT INTO t_parent (SELECT i FROM generate_series(1, 100) AS i);
INSERT INTO t_simple (SELECT i, i, i, i, i FROM generate_series(1, 10) AS i);

-- Test that adding an index on a column updated by the query causes replanning
-- and recomputation of columns to be compared.
PREPARE updateplan1 (INT, INT, INT, INT, INT) AS UPDATE t_simple SET v1 = $2, v2 = $3, v3 = $4, v4 = $5 WHERE k = $1;
-- Step 1: Execute query 5 times to ensure that the plan is cached.
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);

EXPLAIN (ANALYZE, DIST, COSTS OFF) EXECUTE updateplan1 (1, 1, 1, 1, 1);
-- Step 2: Add an index and do not update the column on which the index is defined.
-- If the cached plan is used, additional index write requests will not be be
-- optimized as v3 does not have any other entities on it.
CREATE INDEX NONCONCURRENTLY t_simple_v3 ON t_simple (v3);
EXPLAIN (ANALYZE, DIST, COSTS OFF) EXECUTE updateplan1 (1, 1, 1, 1, 1);


-- Test that dropping an index on a column updated by the query causes replanning
-- and recomputation of columns to be compared.
-- Step 1: Cache the previous plan.
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);
EXECUTE updateplan1 (1, 1, 1, 1, 1);

-- Step 2: Drop the index and update the column on which the index was defined.
-- If the cached plan is used, we will run out of budget to compare v3 resulting
-- in additional index write requests.
SET yb_update_num_cols_to_compare TO 2;
DROP INDEX t_simple_v2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) EXECUTE updateplan1 (1, 1, 2, 1, 1);

EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);

SET yb_update_num_cols_to_compare TO 50;

-- Similar to the above, test that adding a foreign key causes replanning.
ALTER TABLE t_simple ADD CONSTRAINT fk_t_simple_v2 FOREIGN KEY (v2) REFERENCES t_parent(k);
-- Updating v2 should not produce a foreign key check when the value of the column
-- remains unchanged.
EXPLAIN (ANALYZE, DIST, COSTS OFF) EXECUTE updateplan1 (1, 1, 2, 1, 1);

EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);
EXECUTE updateplan1 (1, 1, 2, 1, 1);

-- Similar to the above, test that dropping a foreign key causes replanning.
SET yb_update_num_cols_to_compare TO 2;
ALTER TABLE t_simple DROP CONSTRAINT fk_t_simple_v2;
EXPLAIN (ANALYZE, DIST, COSTS OFF) EXECUTE updateplan1 (1, 1, 2, 1, 1);

DEALLOCATE updateplan1;

DROP TABLE t_simple;
DROP TABLE t_parent;

--
-- #25075: Test INSERT with UPDATE ON CONFLICT DO UPDATE
--
DROP TABLE IF EXISTS ioc_table;
CREATE TABLE ioc_table (id text, uuid_col uuid, name text, primary key ((id, name), uuid_col));
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_table_name_id_uidx ON ioc_table (name, id);
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_table_name_uidx ON ioc_table (name);
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_table_uuid_col_uidx ON ioc_table (uuid_col);

CREATE EXTENSION "uuid-ossp";
-- insert
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO ioc_table VALUES ('user-id-1-1', uuid_generate_v4(), 'Test Name 1')
  ON CONFLICT (id, name) DO UPDATE SET uuid_col = EXCLUDED.uuid_col;
-- conflict: update
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO ioc_table VALUES ('user-id-1-1', uuid_generate_v4(), 'Test Name 1')
  ON CONFLICT (id, name) DO UPDATE SET uuid_col = EXCLUDED.uuid_col;
-- conflict: update again
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO ioc_table VALUES ('user-id-1-1', uuid_generate_v4(), 'Test Name 1')
  ON CONFLICT (id, name) DO UPDATE SET uuid_col = EXCLUDED.uuid_col;
DROP TABLE IF EXISTS ioc_table;
