-- -----------------------------------------------------------------------------
-- TEST 1: Multi-Statement Read-Your-Own-Write (RYOW)
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE ryow_test (id INT PRIMARY KEY, val TEXT);

INSERT INTO ryow_test SELECT i, 'init' FROM generate_series(1, 100) i;

-- If in_txn_limit_ht is not updated by Statement 1, Statement 2 might
-- use a read-time that is logically BEFORE Statement 1's write-time.
SELECT count(*) FROM ryow_test;

-- Statement 3: Update based on previous write
UPDATE ryow_test SET val = 'updated' WHERE id = 50;

-- Verification
SELECT id, val FROM ryow_test WHERE id = 50;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 2: Self-Referencing Insert (INSERT INTO ... SELECT FROM self)
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE self_insert_test (id INT PRIMARY KEY);

INSERT INTO self_insert_test SELECT i FROM generate_series(1, 100000) i;

-- The SELECT must find the first 100000 rows in the Regular DB.
-- The INSERT then writes 100000 new rows (ID 100001-200000) to the Regular DB.
-- This must NOT loop indefinitely.
INSERT INTO self_insert_test SELECT id + 100000 FROM self_insert_test;

-- Final count should be exactly 200000.
SELECT count(*) FROM self_insert_test;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 3: Cross-Statement Visibility with Mixed Operations
-- -----------------------------------------------------------------------------
BEGIN;
CREATE TABLE mixed_visibility_test (id INT PRIMARY KEY, balance INT);

-- First batch
INSERT INTO mixed_visibility_test VALUES (1, 100000), (2, 200000);

-- Use a statement that references the data to perform a calculation
INSERT INTO mixed_visibility_test
SELECT 3, sum(balance) FROM mixed_visibility_test;

SELECT * FROM mixed_visibility_test ORDER BY id;
COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 4: The "Infinite Increment" Test
-- -----------------------------------------------------------------------------
BEGIN;
CREATE TABLE increment_test (id INT PRIMARY KEY, val INT);

-- Initial Data: Single row
INSERT INTO increment_test VALUES (1, 10);

-- Scenario:
-- We want to increment 'val' by 1 for any row where val < 100.
-- 1. Scan finds val=10. 10 < 100 is TRUE.
-- 2. UPDATE writes val=11 directly to Regular DB (Skip Intents).
-- 3. If the scan is NOT isolated, it might find val=11 later.
-- 4. 11 < 100 is still TRUE.
-- 5. UPDATE writes val=12... and so on.

UPDATE increment_test SET val = val + 1 WHERE val < 100000;

-- EXPECTED (Correct Isolation): val = 11
-- ACTUAL (If Isolation Fails): val = 100000 (or the query loops/times out)
SELECT * FROM increment_test;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 5: Interleaved Modifying CTEs Visibility (YB vs PG deviation)
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE mcte_test (id INT PRIMARY KEY);

-- YugabyteDB deviates from PostgreSQL here due to how in_txn_limit is picked (GHI #10142).
-- In PostgreSQL, same-transaction visibility uses CommandId which is fixed before the
-- statement begins, so the read CTE will not see the INSERT from the write CTE.
-- Expected PG result: 0.
-- In YugabyteDB, the in_txn_limit is picked by the read operation. Because the read
-- is forced to execute after the write via LATERAL correlation, the picked in_txn_limit
-- includes the write that just occurred in the same statement.
-- Expected YB result: 1.
WITH
  w_write AS (
    INSERT INTO mcte_test VALUES (10) RETURNING id
  ),
  w_read AS (
    SELECT x.cnt AS c
    FROM w_write w
    CROSS JOIN LATERAL (
      SELECT count(*) AS cnt
      FROM mcte_test g
      WHERE g.id <= w.id
    ) x
  )
SELECT r.c AS count_after_write
FROM w_read r;

COMMIT;

-- -----------------------------------------------------------------------------
-- TEST 6: Statement should not see own writes (Case 1) (GHI #32221)
-- -----------------------------------------------------------------------------
SHOW default_transaction_isolation \gset
SET default_transaction_isolation TO 'read committed';
DROP TABLE IF EXISTS test6_anchor;
CREATE TABLE test6_anchor (id INT PRIMARY KEY);
INSERT INTO test6_anchor VALUES (100);

CREATE OR REPLACE FUNCTION my_func_write() RETURNS int AS $$
BEGIN
  INSERT INTO skip_intents_func_anomaly VALUES (10);
  RETURN 10;
END;
$$ LANGUAGE plpgsql VOLATILE;

BEGIN;
DROP TABLE IF EXISTS skip_intents_func_anomaly;
CREATE TABLE skip_intents_func_anomaly (id INT PRIMARY KEY);

-- The FROM clause reads the anchor table, establishing the statement's read_time.
-- Then it calls my_func_write(), which would perform a skip-intents write if the
-- yb_enable_new_relation_fastpath_write_in_txn_blocks GUC is set.
-- The target list evaluates the subquery, which shouldn't read any data written as
-- part of the same statement, hence returning 0. This holds both with and without
-- the skip-intents optimization, so the output is the same either way.
SELECT
  (SELECT count(*) FROM skip_intents_func_anomaly WHERE id <= t.val) AS count_after_write
FROM (
  SELECT my_func_write() + (existing_cnt * 0) AS val
  FROM (SELECT count(*) as existing_cnt FROM test6_anchor) s1
) t;

ROLLBACK;

-- -----------------------------------------------------------------------------
-- TEST 7: Statement should not see own writes (Case 2) (GHI #32221)
-- -----------------------------------------------------------------------------
DROP TABLE IF EXISTS test7_anchor;
CREATE TABLE test7_anchor (id INT PRIMARY KEY);
INSERT INTO test7_anchor VALUES (100);

CREATE OR REPLACE FUNCTION my_trigger_func() RETURNS trigger AS $$
BEGIN
  INSERT INTO skip_intents_trigger_anomaly VALUES (10);
  RETURN NEW;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- We need an existing table to attach the trigger to
DROP TABLE IF EXISTS trigger_source_table;
CREATE TABLE trigger_source_table (id INT PRIMARY KEY);

CREATE TRIGGER my_trigger
BEFORE INSERT ON trigger_source_table
FOR EACH ROW EXECUTE PROCEDURE my_trigger_func();

BEGIN;
DROP TABLE IF EXISTS skip_intents_trigger_anomaly;
CREATE TABLE skip_intents_trigger_anomaly (id INT PRIMARY KEY);

-- The SELECT from test7_anchor establishes the statement's read_time.
-- The INSERT fires the trigger.
-- The trigger would perform a skip-intents write if the
-- yb_enable_new_relation_fastpath_write_in_txn_blocks GUC is set.
-- The RETURNING clause shouldn't read any data written as part of the same
-- statement, hence returning 0. This holds both with and without the skip-intents
-- optimization, so the output is the same either way.
INSERT INTO trigger_source_table (id)
SELECT 10 + (count(*) * 0) FROM test7_anchor
RETURNING id, (SELECT count(*) FROM skip_intents_trigger_anomaly) AS count_after_write;

ROLLBACK;
-- -----------------------------------------------------------------------------
-- TEST 8: Statement should not see own writes (Case 3) (GHI #32221)
-- -----------------------------------------------------------------------------
DROP TABLE IF EXISTS test8_anchor;
CREATE TABLE test8_anchor (id INT PRIMARY KEY);
INSERT INTO test8_anchor VALUES (10);

CREATE OR REPLACE FUNCTION my_func_write_target(v int) RETURNS int AS $$
BEGIN
  INSERT INTO skip_intents_target VALUES (v);
  RETURN v;
END;
$$ LANGUAGE plpgsql VOLATILE;

BEGIN;
DROP TABLE IF EXISTS skip_intents_target;
CREATE TABLE skip_intents_target(id INT PRIMARY KEY);

-- The SELECT evaluates both the write function and the read subquery.
-- The write inside my_func_write_target() would be a skip-intents write if the
-- yb_enable_new_relation_fastpath_write_in_txn_blocks GUC is set.
-- The read subquery shouldn't read any data written as part of the same statement,
-- hence returning 0. This holds both with and without the skip-intents
-- optimization, so the output is the same either way.
SELECT
  my_func_write_target(id) AS written_val,
  (SELECT count(*) FROM skip_intents_target WHERE id <= anchor.id) AS count_after_write
FROM test8_anchor anchor;

ROLLBACK;

-- -----------------------------------------------------------------------------
-- TEST 9: Statement should not see own writes (Case 4) (GHI #32221)
-- -----------------------------------------------------------------------------
DROP TABLE IF EXISTS test9_anchor;
CREATE TABLE test9_anchor (id INT PRIMARY KEY);
INSERT INTO test9_anchor VALUES (10);

CREATE OR REPLACE FUNCTION my_func_write_where(v int) RETURNS int AS $$
BEGIN
  INSERT INTO skip_intents_where VALUES (v);
  RETURN v;
END;
$$ LANGUAGE plpgsql VOLATILE;

BEGIN;
DROP TABLE IF EXISTS skip_intents_where;
CREATE TABLE skip_intents_where(id INT PRIMARY KEY);

-- The SELECT evaluates the write function in the WHERE clause,
-- and reads the target table in the target list.
-- The write inside my_func_write_where() would be a skip-intents write if the
-- yb_enable_new_relation_fastpath_write_in_txn_blocks GUC is set.
-- The target list shouldn't read any data written as part of the same statement,
-- hence returning 0. This holds both with and without the skip-intents
-- optimization, so the output is the same either way.
SELECT
  (SELECT count(*) FROM skip_intents_where WHERE id <= anchor.id) AS count_after_write
FROM test9_anchor anchor
WHERE my_func_write_where(id) = id;

ROLLBACK;

-- -----------------------------------------------------------------------------
-- TEST 10: Interleaved Nested Write to Existing Table Anomaly (GHI #32221)
-- -----------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION my_trigger_func_mixed() RETURNS trigger AS $$
BEGIN
  IF NEW.id = 2 THEN
    INSERT INTO skip_intents_mixed VALUES (10);
  END IF;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql VOLATILE;

BEGIN;
DROP TABLE IF EXISTS skip_intents_mixed;
CREATE TABLE skip_intents_mixed(id INT PRIMARY KEY);

DROP TABLE IF EXISTS trigger_source_mixed;
CREATE TABLE trigger_source_mixed (id INT PRIMARY KEY);
CREATE TRIGGER my_trigger_mixed
BEFORE INSERT ON trigger_source_mixed
FOR EACH ROW EXECUTE PROCEDURE my_trigger_func_mixed();

-- Row 1: id=1 does not fire the nested write.
-- Row 2: the trigger performs a nested write to another newly created table.
-- The RETURNING subquery then reads the table being written. The output is the same
-- irrespective of whether the skip-intents optimization is used. The count is 1
-- rather than 0 because of a YSQL implementation quirk where writes performed before
-- the first read of a statement are visible to that statement (GHI #10142).
INSERT INTO trigger_source_mixed VALUES (1), (2)
RETURNING id, (SELECT count(*) FROM trigger_source_mixed) AS c;

ROLLBACK;

-- Reset isolation to default
SET default_transaction_isolation TO :'default_transaction_isolation';

-- -----------------------------------------------------------------------------
-- TEST 11: CTAS Repeatable Read Nested Read Anomaly (GHI #32221)
-- -----------------------------------------------------------------------------
SET default_transaction_isolation TO 'repeatable read';
DROP TABLE IF EXISTS test11_anchor;
CREATE TABLE test11_anchor (id INT PRIMARY KEY);
INSERT INTO test11_anchor VALUES (1), (2);

CREATE OR REPLACE FUNCTION my_func_ctas_read(v int) RETURNS int AS $$
DECLARE c INT;
BEGIN
  IF v = 1 THEN
    c := 1;
  END IF;
  IF v = 2 THEN
    -- Row 2 performs a read on the newly created table
    SELECT count(*) INTO c FROM skip_intents_ctas_rr;
  END IF;
  RETURN c;
END;
$$ LANGUAGE plpgsql VOLATILE;

-- Top-level CTAS statement in Repeatable Read. It runs outside an explicit
-- transaction block, so the write would be a skip-intents write if the
-- yb_enable_new_relation_fastpath_write GUC is set (it is on by default).
-- Row 1 evaluates the function and is inserted into the new table.
-- Row 2 evaluates the function, which reads the new table.
-- The output is the same irrespective of whether the skip-intents optimization is
-- used: row 2 still sees the one row written for row 1, because of a YSQL
-- implementation quirk where writes performed before the first read of a statement
-- are visible to that statement (GHI #10142).
DROP TABLE IF EXISTS skip_intents_ctas_rr;
CREATE TABLE skip_intents_ctas_rr AS SELECT my_func_ctas_read(id) AS val FROM test11_anchor;

SELECT * FROM skip_intents_ctas_rr ORDER BY val;

-- Reset isolation to default for subsequent tests
SET default_transaction_isolation TO :'default_transaction_isolation';

-- -----------------------------------------------------------------------------
-- TEST 12: Nested statements see writes of earlier statements (GHI #32221)
-- -----------------------------------------------------------------------------
-- A CTAS statement whose volatile function writes to and then reads the CTAS target
-- table. Each statement inside the function reads at its own in_txn_limit, so it sees
-- the rows written by the statements that ran before it, but not the rows it writes
-- itself. The output is the same irrespective of whether the skip-intents
-- optimization is used.
CREATE TABLE probe_log (i INT PRIMARY KEY, seen BIGINT);
CREATE OR REPLACE FUNCTION write_then_count(i INT) RETURNS INT LANGUAGE plpgsql VOLATILE AS
$$ DECLARE c BIGINT;
BEGIN
  INSERT INTO ctas_target VALUES (i * 1000);
  SELECT count(*) INTO c FROM ctas_target;
  INSERT INTO probe_log VALUES (i, c);
  RETURN i;
END $$;

CREATE TABLE ctas_target AS SELECT write_then_count(g) AS k FROM generate_series(1, 3) g;

SELECT i, seen FROM probe_log ORDER BY i;

DROP TABLE ctas_target;
DROP TABLE probe_log;

-- -----------------------------------------------------------------------------
-- TEST 13: Cursor visibility over a relation created in the transaction
-- (GHI #32221)
-- -----------------------------------------------------------------------------
SET default_transaction_isolation TO 'read committed';

-- Case 1: the cursor is declared before the rows are written. A FETCH reads at the
-- in_txn_limit picked by the portal's first read rather than at the snapshot taken by
-- DECLARE, so rows written between DECLARE and the first FETCH are visible. PostgreSQL
-- would return no rows here because same-transaction visibility is based on CommandId
-- (GHI #10142). The output is the same irrespective of whether the skip-intents
-- optimization is used, because fastpath rows and intents obey the same in_txn_limit
-- based visibility rule.
BEGIN;
CREATE TABLE cursor_new_rel (id INT PRIMARY KEY);
DECLARE cur_before CURSOR FOR SELECT id FROM cursor_new_rel ORDER BY id;
INSERT INTO cursor_new_rel SELECT generate_series(1, 5);
FETCH ALL FROM cur_before;
COMMIT;

-- Case 2: cursor stability. The portal's in_txn_limit is fixed by its first read, so
-- the rows written after the first FETCH are not visible to the remaining FETCHes,
-- while a new statement picks a fresh in_txn_limit and sees all of them.
BEGIN;
CREATE TABLE cursor_stability (id INT PRIMARY KEY);
INSERT INTO cursor_stability SELECT generate_series(1, 3);
DECLARE cur_stable CURSOR FOR SELECT id FROM cursor_stability ORDER BY id;
FETCH 1 FROM cur_stable;
INSERT INTO cursor_stability SELECT generate_series(4, 6);
-- Only the rows that existed as of the first FETCH.
FETCH ALL FROM cur_stable;
-- A new statement sees all six rows.
SELECT count(*) FROM cursor_stability;
COMMIT;

-- Case 3: FOR UPDATE. Explicit row locks are skipped for a relation created in the
-- current transaction when the skip-intents optimization applies, since no other
-- transaction can see that relation. The rows must still be returned and remain
-- updatable by the same transaction.
BEGIN;
CREATE TABLE cursor_for_update (id INT PRIMARY KEY, val INT);
INSERT INTO cursor_for_update SELECT g, g FROM generate_series(1, 3) g;
DECLARE cur_lock CURSOR FOR
  SELECT id, val FROM cursor_for_update ORDER BY id FOR UPDATE;
FETCH ALL FROM cur_lock;
UPDATE cursor_for_update SET val = val * 10 WHERE id = 1;
SELECT id, val FROM cursor_for_update ORDER BY id;
COMMIT;

-- Case 4: WITH HOLD. COMMIT runs the cursor's query and drains its rows into the portal's
-- tuplestore, and that run happens while the transaction that created the relation is
-- still open, so it reads the rows written via the fastpath. The FETCH after COMMIT only
-- reads back the tuplestore.
BEGIN;
CREATE TABLE cursor_with_hold (id INT PRIMARY KEY);
INSERT INTO cursor_with_hold SELECT generate_series(1, 5);
DECLARE cur_hold CURSOR WITH HOLD FOR SELECT id FROM cursor_with_hold ORDER BY id;
COMMIT;
FETCH ALL FROM cur_hold;
CLOSE cur_hold;

-- Case 5: a cursor over a relation whose relfilenode was swapped by an ALTER TABLE
-- rewrite in this transaction. The rewrite writes the new relfilenode via the
-- fastpath, and the cursor must see the rewritten rows.
BEGIN;
CREATE TABLE cursor_rewrite (id INT PRIMARY KEY, val INT);
INSERT INTO cursor_rewrite SELECT g, g FROM generate_series(1, 3) g;
ALTER TABLE cursor_rewrite ADD COLUMN gen_val INT GENERATED ALWAYS AS (val * 2) STORED;
DECLARE cur_rewrite CURSOR FOR
  SELECT id, val, gen_val FROM cursor_rewrite ORDER BY id;
FETCH ALL FROM cur_rewrite;
COMMIT;

-- Case 6: the cursor is declared before the ALTER TABLE rewrite of the relation it
-- reads. The open portal still holds a reference to the relation, so PostgreSQL's
-- CheckTableNotInUse guard rejects the ALTER before any rewrite is attempted and the
-- FETCH never runs. This is a plain PostgreSQL restriction that applies to any ALTER
-- TABLE on a relation with an open cursor in the same session, whether or not the ALTER
-- rewrites the table and whether or not the relation was created in this transaction,
-- so it is unrelated to the skip-intents optimization.
BEGIN;
CREATE TABLE cursor_rewrite_open (id INT PRIMARY KEY, val INT);
INSERT INTO cursor_rewrite_open SELECT g, g FROM generate_series(1, 3) g;
DECLARE cur_rewrite_open CURSOR FOR
  SELECT id, val FROM cursor_rewrite_open ORDER BY id;
ALTER TABLE cursor_rewrite_open
  ADD COLUMN gen_val INT GENERATED ALWAYS AS (val * 2) STORED;
FETCH ALL FROM cur_rewrite_open;
ROLLBACK;

DROP TABLE cursor_new_rel, cursor_stability, cursor_for_update, cursor_with_hold,
           cursor_rewrite;

SET default_transaction_isolation TO :'default_transaction_isolation';
