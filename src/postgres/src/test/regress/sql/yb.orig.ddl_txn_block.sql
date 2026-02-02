CREATE TABLE test1 (id int PRIMARY KEY);

-- Test rollback of DDL+DML transaction block.
BEGIN;
CREATE TABLE test2 (id int);
INSERT INTO test1 VALUES (2);
ROLLBACK;

-- No rows in test1.
SELECT * FROM test1;
-- test2 does not exist.
SELECT * FROM test2;

-- Test commit of DDL+DML transaction block.
BEGIN;
CREATE TABLE test2 (id int);
INSERT INTO test1 VALUES (2);
COMMIT;

SELECT * FROM test1;
SELECT * FROM test2;

-- Test rollback of a block with multiple DDLs
BEGIN;
CREATE TABLE test3 (id int);
CREATE TABLE test4 (id int PRIMARY KEY, b int);
ALTER TABLE test1 ADD COLUMN value text;
INSERT INTO test1 VALUES (3, 'text');
ROLLBACK;

SELECT * FROM test3;
SELECT * FROM test4;
SELECT * FROM test1;

-- Test commit of a block with multiple DDLs
BEGIN;
CREATE TABLE test3 (id int);
CREATE TABLE test4 (id int);
ALTER TABLE test1 ADD COLUMN value text;
INSERT INTO test1 VALUES (3, 'text');
COMMIT;

SELECT * FROM test3;
SELECT * FROM test4;
SELECT * FROM test1;

-- Same test as above but the first statement is a DML
BEGIN;
INSERT INTO test1 VALUES (5, 'text');
CREATE TABLE test5 (id int);
CREATE TABLE test6 (id int);
ALTER TABLE test1 ADD COLUMN value1 text;
INSERT INTO test1 VALUES (4, 'text', 'text2');
ROLLBACK;

SELECT * FROM test5;
SELECT * FROM test6;
SELECT * FROM test1;

BEGIN;
INSERT INTO test1 VALUES (5, 'text');
CREATE TABLE test5 (id int);
CREATE TABLE test6 (id int);
ALTER TABLE test1 ADD COLUMN value1 text;
INSERT INTO test1 VALUES (4, 'text', 'text2');
COMMIT;

SELECT * FROM test5;
SELECT * FROM test6;
SELECT * FROM test1;

CREATE INDEX ON test1(value);
SELECT value FROM test1 WHERE value='text';

-- Test that schema version bump in case of alter table rollback is handled.
CREATE TABLE test7 (a int primary key, b int);
BEGIN;
INSERT INTO test7 VALUES (1, 1);
INSERT INTO test7 VALUES (2, 2);
ALTER TABLE test7 ADD COLUMN c int;
INSERT INTO test7 VALUES (3, 3, 3);
ROLLBACK;
BEGIN;
INSERT INTO test7 VALUES (1, 1);
COMMIT;
SELECT * FROM test7;

SET allow_system_table_mods = on;
BEGIN;
-- Truncate system table inside a transaction block.
TRUNCATE pg_extension;
ROLLBACK;
RESET allow_system_table_mods;

SET yb_enable_alter_table_rewrite = off;
BEGIN;
-- Truncate user table inside a transaction block with table rewrite disabled.
TRUNCATE test7;
ROLLBACK;
RESET yb_enable_alter_table_rewrite;

-- Rollback CREATE, DROP and CREATE TABLE with same name in a transaction block.
BEGIN;
CREATE TABLE test8 (a int primary key, b int);
INSERT INTO test8 VALUES (1, 1);
SELECT * FROM test8;
DROP TABLE test8;
CREATE TABLE test8 (c int primary key, d int);
INSERT INTO test8 VALUES (10, 10);
ROLLBACK;
SELECT * FROM test8;

-- Same test as above but with COMMIT.
BEGIN;
CREATE TABLE test8 (a int primary key, b int);
INSERT INTO test8 VALUES (1, 1);
SELECT * FROM test8;
DROP TABLE test8;
CREATE TABLE test8 (c int primary key, d int);
INSERT INTO test8 VALUES (10, 10);
COMMIT;
SELECT * FROM test8;

-- Rollback of DROP TABLE.
CREATE TABLE test9 (a int primary key, b int);
INSERT INTO test9 VALUES (1, 1);
BEGIN;
INSERT INTO test9 VALUES (2, 2);
SELECT * FROM test9;
DROP TABLE test9;
ROLLBACK;
SELECT * FROM test9;

-- Rollback of CREATE INDEX should work.
CREATE TABLE test10(id INT PRIMARY KEY, val TEXT);
BEGIN;
CREATE INDEX test10_idx ON test10(val);
\d+ test10;
ROLLBACK;
\d+ test10;

-- TODO(#3109): CREATE and DROP database are already being tested in various
-- other regress tests. This is being tested here since
-- yb_ddl_transaction_block_enabled is false for all of them.
-- Remove this once yb_ddl_transaction_block_enabled is true by
-- default.
create database k1;
drop database k1;

CREATE SEQUENCE regtest_seq;
BEGIN;
DROP SEQUENCE regtest_seq;
COMMIT;

CREATE TABLE test11(id INT PRIMARY KEY, val TEXT);
INSERT INTO test11 VALUES (1, 'text');
BEGIN;
TRUNCATE test11;
TRUNCATE test11;
SELECT * FROM test11;
ROLLBACK;
SELECT * FROM test11;

-- Savepoint allowed without any DDL.
CREATE TABLE test12 (a int primary key, b int);
BEGIN;
INSERT INTO test12 VALUES (1, 1);
SAVEPOINT test12_sp;
INSERT INTO test12 VALUES (2, 2);
SELECT * FROM test12;
ROLLBACK TO SAVEPOINT test12_sp;
COMMIT;
SELECT * FROM test12;

-- DDL after Savepoint disallowed.
BEGIN;
INSERT INTO test12 VALUES (3, 3);
SAVEPOINT test12_sp;
CREATE TABLE test13 (a int primary key, b int);
ROLLBACK;

BEGIN;
CREATE TEMPORARY TABLE temp_table (
    a INT PRIMARY KEY
) ON COMMIT DELETE ROWS;
INSERT INTO temp_table VALUES (1);
INSERT INTO temp_table VALUES (2);
SELECT * FROM temp_table;
COMMIT;
SELECT * FROM temp_table;

BEGIN;
CREATE TEMP TABLE temp_table_commit_drop (
    id INT PRIMARY KEY
)
ON COMMIT DROP;
INSERT INTO temp_table_commit_drop VALUES (1);
SELECT * FROM temp_table_commit_drop;
COMMIT;
SELECT * FROM temp_table_commit_drop;
ANALYZE test1, test2, test3;
CREATE INDEX test1_idx ON test1(id);

CREATE TABLE sales_data (
    sale_id INT,
    sale_date DATE,
    amount DECIMAL(10, 2)
) PARTITION BY RANGE (sale_date);
CREATE TABLE sales_data_202401 PARTITION OF sales_data
    FOR VALUES FROM ('2024-01-01') TO ('2024-02-01');
INSERT INTO sales_data (sale_id, sale_date, amount) VALUES
    (1, '2024-01-10', 100.50),
    (2, '2024-01-25', 75.20);
CREATE TABLE sales_data_202402 PARTITION OF sales_data
    FOR VALUES FROM ('2024-02-01') TO ('2024-03-01');
INSERT INTO sales_data (sale_id, sale_date, amount) VALUES
    (3, '2024-02-05', 120.00);
ALTER TABLE sales_data DETACH PARTITION sales_data_202401 CONCURRENTLY;

-- #27859: Statements which do not require a Perform call before DocDB schema
-- changes should succeed.
CREATE TABLE test_14 (
    id INT PRIMARY KEY,
    name TEXT,
    val INT
);
INSERT INTO test_14 (id, name, val) VALUES
(1, 'Alice', 100),
(2, 'Bob', 200);
CREATE ROLE test_14_user LOGIN;
ALTER TABLE test_14 OWNER TO test_14_user;

-- #27859: Same test case as above but with enable/disable trigger.
CREATE TABLE test_trigger_table (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    age INT,
    email TEXT,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE TABLE trigger_log (
    action TEXT,
    row_id INT,
    changed_at TIMESTAMPTZ DEFAULT NOW()
);
CREATE OR REPLACE FUNCTION log_changes()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO trigger_log(action, row_id)
    VALUES (TG_OP, NEW.id);
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;
CREATE TRIGGER trg_log_changes
AFTER INSERT OR UPDATE OR DELETE ON test_trigger_table
FOR EACH ROW
EXECUTE FUNCTION log_changes();
INSERT INTO test_trigger_table(name, age, email) VALUES ('Alice', 30, 'alice@example.com');
ALTER TABLE test_trigger_table DISABLE TRIGGER trg_log_changes;
INSERT INTO test_trigger_table(name, age, email) VALUES ('Charlie', 28, 'charlie@example.com');
ALTER TABLE test_trigger_table ENABLE TRIGGER trg_log_changes;
SELECT id, name, age, email FROM test_trigger_table;
SELECT action, row_id FROM trigger_log;

-- #29058: REINDEX of partitioned table works.
CREATE TABLE test_partitioned (i int, j int) PARTITION BY LIST (j);
CREATE INDEX NONCONCURRENTLY ON test_partitioned (i);
CREATE TABLE test_partitioned_odd PARTITION OF test_partitioned FOR VALUES IN (1, 3, 5, 7, 9);
CREATE TABLE test_partitioned_even PARTITION OF test_partitioned FOR VALUES IN (2, 4, 6, 8);
INSERT INTO test_partitioned SELECT (2 * g), g FROM generate_series(1, 9) g;
-- Mark all partitions are invalid before REINDEX.
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'test_partitioned_i_idx'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'test_partitioned_odd_i_idx'::regclass;
UPDATE pg_index SET indisvalid = false
    WHERE indexrelid = 'test_partitioned_even_i_idx'::regclass;
\c
-- https://github.com/yugabyte/yugabyte-db/issues/29534
-- The previous \c can be too fast due to incremental relcache refresh optimization.
-- This can cause read restart because a read time of the next REINDEX statement
-- is selected using safetime mechanism which is slightly in the past. If the read
-- time picked is older than the write time of the above UPDATE pg_index statement,
-- we will see restart read error, which is intercepted by PG and shows up as
-- "ERROR:  Restarting a DDL transaction not supported".
-- To avoid read restart error, sleep 2 seconds to allow safetime to advance past
-- the write time of the above UPDATE statement.
SELECT pg_sleep(2);
REINDEX INDEX test_partitioned_i_idx;

\c
-- #29424
CREATE TABLE _33_s_1_data (id INT PRIMARY KEY);

CREATE TABLE _33_s_1_audit (log_id SERIAL PRIMARY KEY, data_id INT, notes TEXT);

CREATE FUNCTION _33_func_audit() RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO _33_s_1_audit (data_id, notes) VALUES (NEW.id, 'V1');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER _33_trig_data
AFTER INSERT ON _33_s_1_data
FOR EACH ROW EXECUTE FUNCTION _33_func_audit();

CREATE PROCEDURE _33_sp_insert(val INT) LANGUAGE SQL AS $$
INSERT INTO _33_s_1_data (id) VALUES (val);
$$;

CALL _33_sp_insert(1);

SELECT COUNT(*) FROM _33_s_1_audit WHERE notes = 'V1';

BEGIN ISOLATION LEVEL REPEATABLE READ;

CREATE OR REPLACE FUNCTION _33_func_audit() RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO _33_s_1_audit (data_id, notes) VALUES (NEW.id, 'V2');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE PROCEDURE _33_sp_insert(val INT) LANGUAGE SQL AS $$
INSERT INTO _33_s_1_data (id) VALUES (val * 100);
$$;

CALL _33_sp_insert(2);

SELECT COUNT(*) FROM _33_s_1_data WHERE id = 200;

SELECT COUNT(*) FROM _33_s_1_audit WHERE notes = 'V2';

SELECT * FROM _33_s_1_audit WHERE notes = 'V2';

CREATE OR REPLACE PROCEDURE _33_sp_insert(val INT) LANGUAGE SQL AS $$
INSERT INTO _33_s_1_data (id) VALUES (val * 200);
$$;

CREATE OR REPLACE FUNCTION _33_func_audit() RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO _33_s_1_audit (data_id, notes) VALUES (NEW.id, 'V3');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CALL _33_sp_insert(3);

SELECT * FROM _33_s_1_audit WHERE notes = 'V3';

-- #29325: Failed ALTER TABLE ALTER TYPE should not lead to a crash in
-- transaction abort.
CREATE TABLE int4_table(id SERIAL, c1 int4, PRIMARY KEY (id ASC));
ALTER TABLE int4_table ALTER c1 TYPE int8;
INSERT INTO int4_table(c1) VALUES (2 ^ 40);
ALTER TABLE int4_table ALTER c1 TYPE int4; -- should fail.
