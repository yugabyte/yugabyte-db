--
-- YB-specific tests for transition tables (REFERENCING clause)
-- Covers edge cases not in upstream PG regress tests.
--

-- Helper function to log transition table contents
CREATE FUNCTION tt_log_insert() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'tt_log_insert: tg_op=%, new_table=%',
    TG_OP,
    (SELECT string_agg(new_table::text, ', ' ORDER BY id) FROM new_table);
  RETURN NULL;
END;
$$;

CREATE FUNCTION tt_log_update() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'tt_log_update: tg_op=%, old_table=%, new_table=%',
    TG_OP,
    (SELECT string_agg(old_table::text, ', ' ORDER BY id) FROM old_table),
    (SELECT string_agg(new_table::text, ', ' ORDER BY id) FROM new_table);
  RETURN NULL;
END;
$$;

CREATE FUNCTION tt_log_delete() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'tt_log_delete: tg_op=%, old_table=%',
    TG_OP,
    (SELECT string_agg(old_table::text, ', ' ORDER BY id) FROM old_table);
  RETURN NULL;
END;
$$;

----------------------------------------------------------------------
-- Section 1: Basic operations
----------------------------------------------------------------------

CREATE TABLE tt_test (id int PRIMARY KEY, val text);
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b'), (3, 'c'), (4, 'd');

CREATE TRIGGER tt_test_ins AFTER INSERT ON tt_test
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert();

CREATE TRIGGER tt_test_upd AFTER UPDATE ON tt_test
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_update();

CREATE TRIGGER tt_test_del AFTER DELETE ON tt_test
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();

-- Plain INSERT with transition table
INSERT INTO tt_test VALUES (10, 'ten');

-- Plain UPDATE with transition tables
UPDATE tt_test SET val = 'updated_' || val WHERE id = 1;

-- Plain DELETE with REFERENCING OLD TABLE
DELETE FROM tt_test WHERE id = 2;

-- DELETE WHERE no rows match (empty transition table)
DELETE FROM tt_test WHERE id = 999;

-- UPDATE WHERE no rows match (empty transition table)
UPDATE tt_test SET val = 'nope' WHERE id = 999;

-- INSERT with no rows (empty transition table via INSERT ... SELECT)
INSERT INTO tt_test SELECT * FROM tt_test WHERE id < 0;

----------------------------------------------------------------------
-- Section 2: CTE (modifying WITH) edge cases
----------------------------------------------------------------------

-- Reset data
TRUNCATE tt_test;
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b'), (3, 'c'), (4, 'd');

-- CTE: single DELETE with transition tables
WITH del AS (DELETE FROM tt_test WHERE id = 1 RETURNING *)
SELECT * FROM del;

-- CTE: single UPDATE with transition tables
WITH upd AS (UPDATE tt_test SET val = 'changed_b' WHERE id = 2 RETURNING *)
SELECT * FROM upd;

-- Re-insert deleted row for subsequent tests
INSERT INTO tt_test VALUES (1, 'a_new');

-- CTE: INSERT + UPDATE in one CTE
WITH
  ins AS (INSERT INTO tt_test VALUES (5, 'five') RETURNING *),
  upd AS (UPDATE tt_test SET val = 'changed_c' WHERE id = 3 RETURNING *)
SELECT * FROM ins UNION ALL SELECT * FROM upd;

-- CTE: INSERT + DELETE in one CTE
WITH
  ins AS (INSERT INTO tt_test VALUES (6, 'six') RETURNING *),
  del AS (DELETE FROM tt_test WHERE id = 4 RETURNING *)
SELECT * FROM ins UNION ALL SELECT * FROM del;

-- CTE: INSERT + UPDATE + DELETE in one CTE
WITH
  ins AS (INSERT INTO tt_test VALUES (7, 'seven') RETURNING *),
  upd AS (UPDATE tt_test SET val = 'changed_a' WHERE id = 1 RETURNING *),
  del AS (DELETE FROM tt_test WHERE id = 5 RETURNING *)
SELECT * FROM ins
UNION ALL SELECT * FROM upd
UNION ALL SELECT * FROM del;

-- CTE: two UPDATEs on same table (merged into one trigger)
WITH
  upd1 AS (UPDATE tt_test SET val = 'first_' || val WHERE id IN (2, 3) RETURNING *),
  upd2 AS (UPDATE tt_test SET val = 'second_' || val WHERE id IN (6, 7) RETURNING *)
SELECT * FROM upd1 UNION ALL SELECT * FROM upd2;

-- CTE: two DELETEs on same table (merged into one trigger)
WITH
  del1 AS (DELETE FROM tt_test WHERE id = 6 RETURNING *),
  del2 AS (DELETE FROM tt_test WHERE id = 7 RETURNING *)
SELECT * FROM del1 UNION ALL SELECT * FROM del2;

-- CTE: two INSERTs on same table (merged -- regression test)
WITH
  ins1 AS (INSERT INTO tt_test VALUES (20, 'twenty') RETURNING *),
  ins2 AS (INSERT INTO tt_test VALUES (21, 'twenty_one') RETURNING *)
SELECT * FROM ins1 UNION ALL SELECT * FROM ins2;

-- CTE: UPDATE ... RETURNING used by outer SELECT
WITH upd AS (UPDATE tt_test SET val = 'ret_' || val WHERE id = 1 RETURNING id, val)
SELECT id, val FROM upd;

-- CTE: DELETE ... RETURNING used by outer SELECT
WITH del AS (DELETE FROM tt_test WHERE id = 20 RETURNING id, val)
SELECT id, val FROM del;

----------------------------------------------------------------------
-- Section 3: UPDATE ... FROM / DELETE ... USING
----------------------------------------------------------------------

TRUNCATE tt_test;
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b'), (3, 'c');

CREATE TABLE tt_source (id int, new_val text);
INSERT INTO tt_source VALUES (1, 'from_a'), (2, 'from_b');

-- UPDATE ... FROM with transition tables
UPDATE tt_test SET val = s.new_val FROM tt_source s WHERE tt_test.id = s.id;

-- DELETE ... USING with transition tables
DELETE FROM tt_test USING tt_source s WHERE tt_test.id = s.id;

-- Two separate UPDATE ... FROM statements (each fires own trigger)
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b');
UPDATE tt_test SET val = 'first_pass' WHERE id = 1;
UPDATE tt_test SET val = 'second_pass' WHERE id = 2;

DROP TABLE tt_source;

----------------------------------------------------------------------
-- Section 4: Transaction and error handling
----------------------------------------------------------------------

TRUNCATE tt_test;
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b');

-- Transition tables inside explicit transaction
BEGIN;
UPDATE tt_test SET val = 'txn_' || val WHERE id = 1;
INSERT INTO tt_test VALUES (10, 'txn_new');
DELETE FROM tt_test WHERE id = 2;
COMMIT;

-- SAVEPOINT + ROLLBACK TO with transition tables
INSERT INTO tt_test VALUES (2, 'b_restored');
BEGIN;
SAVEPOINT sp1;
UPDATE tt_test SET val = 'will_rollback' WHERE id = 1;
ROLLBACK TO sp1;
UPDATE tt_test SET val = 'after_rollback' WHERE id = 1;
COMMIT;

-- Trigger function raises exception — rollback behavior
CREATE FUNCTION tt_raise_error() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE EXCEPTION 'intentional error in trigger';
  RETURN NULL;
END;
$$;

CREATE TRIGGER tt_test_err AFTER UPDATE ON tt_test
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_raise_error();

BEGIN;
SAVEPOINT sp1;
UPDATE tt_test SET val = 'should_fail' WHERE id = 1;  -- error expected
ROLLBACK TO sp1;
COMMIT;

DROP TRIGGER tt_test_err ON tt_test;
DROP FUNCTION tt_raise_error();

-- Multiple statements in a transaction each with transition triggers
BEGIN;
INSERT INTO tt_test VALUES (30, 'thirty');
UPDATE tt_test SET val = 'multi_txn_' || val WHERE id = 1;
DELETE FROM tt_test WHERE id = 30;
COMMIT;

----------------------------------------------------------------------
-- Section 5: Data edge cases
----------------------------------------------------------------------

TRUNCATE tt_test;

-- Transition tables with NULL values
TRUNCATE tt_test;
INSERT INTO tt_test VALUES (1, NULL), (2, NULL);
UPDATE tt_test SET val = NULL WHERE id = 1;
DELETE FROM tt_test WHERE id = 2;

-- Transition tables with various data types
CREATE TABLE tt_types (
  id int PRIMARY KEY,
  t text,
  j jsonb,
  a int[]
);

CREATE TRIGGER tt_types_ins AFTER INSERT ON tt_types
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert();

CREATE TRIGGER tt_types_upd AFTER UPDATE ON tt_types
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_update();

CREATE TRIGGER tt_types_del AFTER DELETE ON tt_types
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();

INSERT INTO tt_types VALUES (1, 'hello', '{"key": "value"}', '{1,2,3}');
UPDATE tt_types SET j = '{"updated": true}' WHERE id = 1;
DELETE FROM tt_types WHERE id = 1;

DROP TABLE tt_types;

-- Trigger function queries the transition table multiple times
CREATE FUNCTION tt_multi_query() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE
  cnt int;
  vals text;
BEGIN
  SELECT count(*) INTO cnt FROM new_table;
  SELECT string_agg(val, ', ' ORDER BY id) INTO vals FROM new_table;
  RAISE NOTICE 'count=%, vals=%', cnt, vals;
  RETURN NULL;
END;
$$;

TRUNCATE tt_test;
CREATE TRIGGER tt_test_multi AFTER INSERT ON tt_test
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_multi_query();

INSERT INTO tt_test VALUES (1, 'first'), (2, 'second'), (3, 'third');

DROP TRIGGER tt_test_multi ON tt_test;
DROP FUNCTION tt_multi_query();

----------------------------------------------------------------------
-- Section 6: Multiple triggers and trigger ordering
----------------------------------------------------------------------

-- Multiple statement-level triggers on same event
CREATE FUNCTION tt_log_insert_2() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'tt_log_insert_2: tg_name=%, new_table=%',
    TG_NAME,
    (SELECT string_agg(new_table::text, ', ' ORDER BY id) FROM new_table);
  RETURN NULL;
END;
$$;

TRUNCATE tt_test;

CREATE TRIGGER tt_test_ins_2 AFTER INSERT ON tt_test
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert_2();

INSERT INTO tt_test VALUES (1, 'multi_trigger');

DROP TRIGGER tt_test_ins_2 ON tt_test;
DROP FUNCTION tt_log_insert_2();

-- Row-level + statement-level triggers
CREATE FUNCTION tt_row_trigger() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'row trigger: id=%, val=%', NEW.id, NEW.val;
  RETURN NEW;
END;
$$;

TRUNCATE tt_test;

CREATE TRIGGER tt_test_row_ins AFTER INSERT ON tt_test
  FOR EACH ROW EXECUTE FUNCTION tt_row_trigger();

INSERT INTO tt_test VALUES (1, 'row_and_stmt'), (2, 'row_and_stmt_2');

DROP TRIGGER tt_test_row_ins ON tt_test;
DROP FUNCTION tt_row_trigger();

----------------------------------------------------------------------
-- Section 7: COPY
----------------------------------------------------------------------

TRUNCATE tt_test;

-- COPY FROM with INSERT transition table trigger
COPY tt_test FROM stdin;
1	copy_a
2	copy_b
3	copy_c
\.

-- COPY FROM with index present (different code path)
CREATE INDEX ON tt_test(val);

COPY tt_test FROM stdin;
4	copy_d
5	copy_e
\.

DROP INDEX tt_test_val_idx;

-- COPY with row batching requested.  The test harness disables batching by
-- default, so request it explicitly.  A statement-level transition trigger
-- forces a single transaction (with a WARNING), so the trigger fires once
-- with every copied row.
CREATE TABLE tt_copy (id int PRIMARY KEY, val text);
CREATE TRIGGER tt_copy_ins AFTER INSERT ON tt_copy
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert();

SET yb_default_copy_from_rows_per_transaction = 2;
COPY tt_copy FROM stdin;
1	batch_a
2	batch_b
3	batch_c
4	batch_d
5	batch_e
\.
RESET yb_default_copy_from_rows_per_transaction;

COPY tt_copy FROM stdin WITH (ROWS_PER_TRANSACTION 2);
6	rpt_a
7	rpt_b
8	rpt_c
9	rpt_d
10	rpt_e
\.

-- COPY ... WHERE: new_table holds only the rows that pass the filter
COPY tt_copy FROM stdin WHERE id % 2 = 0;
11	where_a
12	where_b
13	where_c
14	where_d
\.

SELECT * FROM tt_copy ORDER BY id;

DROP TABLE tt_copy;

----------------------------------------------------------------------
-- Section 8: Tuplestore spill behavior
----------------------------------------------------------------------

-- Statement trigger transition table spills to disk under low work_mem
--
-- With work_mem=64kB, inserting 2000 rows of ~5kB each into a table that has
-- a transition-table trigger forces the tuplestore to spill to disk.  We
-- extract the "temp written=N" value from EXPLAIN (ANALYZE, BUFFERS) and
-- assert it exceeds 1000 blocks.
CREATE FUNCTION tt_explain_temp_written(query text) RETURNS int LANGUAGE plpgsql AS $$
DECLARE
  line text;
  matches text[];
  temp_written int := 0;
BEGIN
  FOR line IN EXECUTE query LOOP
    matches := regexp_match(line, 'temp(?: read=[0-9]+)? written=([0-9]+)');
    IF matches IS NOT NULL THEN
      temp_written := GREATEST(temp_written, matches[1]::int);
    END IF;
  END LOOP;

  RETURN temp_written;
END;
$$;

CREATE TABLE tt_spill_test (id int PRIMARY KEY, payload text);
SET work_mem = '64kB';

CREATE FUNCTION tt_spill_count() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE
  cnt bigint;
BEGIN
  SELECT count(*) INTO cnt FROM new_table;
  RETURN NULL;
END;
$$;

CREATE TRIGGER tt_spill_ins AFTER INSERT ON tt_spill_test
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_spill_count();

SELECT tt_explain_temp_written($q$
  EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, SUMMARY OFF, TIMING OFF)
  INSERT INTO tt_spill_test
  SELECT g, repeat('x', 5000)
  FROM generate_series(1, 2000) AS g
$q$) > 1000 AS spill_to_disk;

RESET work_mem;
DROP TABLE tt_spill_test;
DROP FUNCTION tt_spill_count();
DROP FUNCTION tt_explain_temp_written(text);

----------------------------------------------------------------------
-- Section 9: System columns referenced from transition tables
----------------------------------------------------------------------

TRUNCATE tt_test;

CREATE FUNCTION tt_log_sys_cols() RETURNS trigger LANGUAGE plpgsql AS $$
DECLARE
  s text;
BEGIN
  IF (TG_OP = 'INSERT') THEN
    SELECT string_agg(format('id=%s,tableoid_ok=%s', id, tableoid = 'tt_test'::regclass), '; ' ORDER BY id)
      INTO s FROM new_table;
    RAISE NOTICE 'sys ins: %', s;
  ELSIF (TG_OP = 'DELETE') THEN
    SELECT string_agg(format('id=%s,tableoid_ok=%s', id, tableoid = 'tt_test'::regclass), '; ' ORDER BY id)
      INTO s FROM old_table;
    RAISE NOTICE 'sys del: %', s;
  END IF;
  RETURN NULL;
END;
$$;

CREATE TRIGGER tt_test_sys_ins AFTER INSERT ON tt_test
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_sys_cols();

CREATE TRIGGER tt_test_sys_del AFTER DELETE ON tt_test
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_sys_cols();

-- tableoid is accessible from transition table rows
INSERT INTO tt_test VALUES (1, 'sys_a'), (2, 'sys_b');
DELETE FROM tt_test WHERE id = 1;

DROP TRIGGER tt_test_sys_ins ON tt_test;
DROP TRIGGER tt_test_sys_del ON tt_test;
DROP FUNCTION tt_log_sys_cols();

----------------------------------------------------------------------
-- Section 10: Views and transition tables
----------------------------------------------------------------------

TRUNCATE tt_test;
INSERT INTO tt_test VALUES (1, 'a'), (2, 'b');

-- REFERENCING not allowed on INSTEAD OF triggers on views
CREATE VIEW tt_test_v AS SELECT * FROM tt_test;

CREATE FUNCTION tt_view_instead() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RETURN NULL;
END;
$$;

-- expected: ERROR (REFERENCING clause not allowed with INSTEAD OF triggers)
CREATE TRIGGER tt_view_bad
  AFTER INSERT ON tt_test_v
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_view_instead();

CREATE TRIGGER tt_view_bad
  INSTEAD OF INSERT ON tt_test_v
  REFERENCING NEW TABLE AS new_table
  FOR EACH ROW EXECUTE FUNCTION tt_view_instead();

DROP FUNCTION tt_view_instead();

-- Auto-updatable view fires base table's transition-table trigger
INSERT INTO tt_test_v VALUES (10, 'via_view');
UPDATE tt_test_v SET val = 'via_view_upd' WHERE id = 10;
DELETE FROM tt_test_v WHERE id = 10;

DROP VIEW tt_test_v;

----------------------------------------------------------------------
-- Section 11: Inheritance / partition DELETE behavior
----------------------------------------------------------------------

-- DELETE on a leaf partition does NOT fire root-level statement
-- trigger (matches vanilla PG).  Trigger on root has transition table; we
-- DELETE directly from the leaf and expect no NOTICE from the root trigger.
CREATE TABLE tt_orders (id int, region text, qty int, PRIMARY KEY (id, region))
  PARTITION BY LIST (region);
CREATE TABLE tt_orders_us PARTITION OF tt_orders FOR VALUES IN ('US');
CREATE TABLE tt_orders_eu PARTITION OF tt_orders FOR VALUES IN ('EU');

INSERT INTO tt_orders VALUES (1, 'US', 5), (2, 'US', 25), (3, 'EU', 50);

CREATE TRIGGER tt_orders_del AFTER DELETE ON tt_orders
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();

-- DELETE on the root -- trigger fires
DELETE FROM tt_orders WHERE qty >= 20;

-- DELETE addressed directly to a leaf -- root trigger must NOT fire
INSERT INTO tt_orders VALUES (4, 'EU', 30);
DELETE FROM tt_orders_eu WHERE qty >= 20;

DROP TABLE tt_orders;

----------------------------------------------------------------------
-- Section 12: Multi-level (>2) hierarchy DELETE behavior
----------------------------------------------------------------------

-- Statement-level (transition table) triggers fire ONLY on the relation
-- explicitly named in the DML -- never on an ancestor above it, an
-- intermediate parent below it, or a leaf below it.  This is critical for
-- the YB planner fix in add_row_identity_columns(): when emitting the
-- wholerow junk attribute for a child relation, only the query's
-- resultRelation (the SQL-named target) needs to be consulted; intermediate
-- parents in the hierarchy cannot contribute a transition-table trigger.

CREATE FUNCTION tt_log_delete_mh() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'fired: trigger=%, tg_table=%, old_table=%',
    TG_NAME, TG_TABLE_NAME,
    (SELECT string_agg(old_table::text, ', ' ORDER BY id) FROM old_table);
  RETURN NULL;
END;
$$;

-- 4-level partition hierarchy: tt_mh_a > tt_mh_b > tt_mh_c > tt_mh_d
CREATE TABLE tt_mh_a (id int, region text, qty int, PRIMARY KEY (id, region))
  PARTITION BY LIST (region);
CREATE TABLE tt_mh_b PARTITION OF tt_mh_a FOR VALUES IN ('US','EU')
  PARTITION BY LIST (region);
CREATE TABLE tt_mh_c PARTITION OF tt_mh_b FOR VALUES IN ('US')
  PARTITION BY LIST (region);
CREATE TABLE tt_mh_d PARTITION OF tt_mh_c FOR VALUES IN ('US');

CREATE TRIGGER tt_mh_a_del AFTER DELETE ON tt_mh_a
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_mh_b_del AFTER DELETE ON tt_mh_b
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_mh_c_del AFTER DELETE ON tt_mh_c
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_mh_d_del AFTER DELETE ON tt_mh_d
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();

INSERT INTO tt_mh_a VALUES (1,'US',10),(2,'US',20),(3,'US',30),(4,'US',40);

-- DELETE FROM tt_mh_b (intermediate) -- only tt_mh_b_del fires
DELETE FROM tt_mh_b WHERE qty IN (10, 20);

-- DELETE FROM tt_mh_c (intermediate) -- only tt_mh_c_del fires
DELETE FROM tt_mh_c WHERE qty = 30;

-- DELETE FROM tt_mh_a (root) -- only tt_mh_a_del fires
DELETE FROM tt_mh_a WHERE qty = 40;

-- DELETE FROM tt_mh_d (leaf) -- only tt_mh_d_del fires
INSERT INTO tt_mh_a VALUES (5,'US',50);
DELETE FROM tt_mh_d WHERE qty = 50;

DROP TABLE tt_mh_a;

-- 4-level inheritance hierarchy: tt_ih_a > tt_ih_b > tt_ih_c > tt_ih_d
CREATE TABLE tt_ih_a (id int, val text);
CREATE TABLE tt_ih_b () INHERITS (tt_ih_a);
CREATE TABLE tt_ih_c () INHERITS (tt_ih_b);
CREATE TABLE tt_ih_d () INHERITS (tt_ih_c);

CREATE TRIGGER tt_ih_a_del AFTER DELETE ON tt_ih_a
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_ih_b_del AFTER DELETE ON tt_ih_b
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_ih_c_del AFTER DELETE ON tt_ih_c
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();
CREATE TRIGGER tt_ih_d_del AFTER DELETE ON tt_ih_d
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete_mh();

INSERT INTO tt_ih_a VALUES (1,'a1');
INSERT INTO tt_ih_b VALUES (2,'b1');
INSERT INTO tt_ih_c VALUES (3,'c1');
INSERT INTO tt_ih_d VALUES (4,'d1');

-- DELETE FROM tt_ih_b -- hits rows from B,C,D; only tt_ih_b_del fires
DELETE FROM tt_ih_b;

INSERT INTO tt_ih_c VALUES (3,'c1');
INSERT INTO tt_ih_d VALUES (4,'d1');

-- DELETE FROM tt_ih_c -- hits rows from C,D; only tt_ih_c_del fires
DELETE FROM tt_ih_c;

DROP TABLE tt_ih_a, tt_ih_b, tt_ih_c, tt_ih_d;
DROP FUNCTION tt_log_delete_mh();

----------------------------------------------------------------------
-- Section 13: IOCDU on partition with reordered + dropped columns
----------------------------------------------------------------------

-- Regression test for the fix in YbFlushSlotsFromBatch: the deferred
-- IOC batching path must restore tcs_original_insert_tuple to point at
-- the deep-copied root-format planSlot we kept, not the stale slot
-- ExecPrepareTupleRouting stashed at routing time.  If the slot's
-- tuple descriptor doesn't match the root's, the transition table
-- captures garbled columns -- visible here as wrong per-column values
-- or as a crash when varlena lengths are mismatched.

CREATE TABLE tt_ioc_root (a int UNIQUE, b text, c numeric)
  PARTITION BY LIST (a);

-- Child has a dropped column AND reordered remaining columns
CREATE TABLE tt_ioc_child (xdrop int, b text, c numeric, a int UNIQUE);
ALTER TABLE tt_ioc_child DROP COLUMN xdrop;
ALTER TABLE tt_ioc_root ATTACH PARTITION tt_ioc_child FOR VALUES IN (1,2,3,4);

CREATE FUNCTION tt_ioc_cap() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'UPDATE' THEN
    RAISE NOTICE 'UPDATE old per-col: %',
      (SELECT string_agg(format('a=%s/b=%s/c=%s', a, b, c), ', ' ORDER BY a)
         FROM old_table);
    RAISE NOTICE 'UPDATE new per-col: %',
      (SELECT string_agg(format('a=%s/b=%s/c=%s', a, b, c), ', ' ORDER BY a)
         FROM new_table);
  ELSE
    RAISE NOTICE 'INSERT new per-col: %',
      (SELECT string_agg(format('a=%s/b=%s/c=%s', a, b, c), ', ' ORDER BY a)
         FROM new_table);
  END IF;
  RETURN NULL;
END;
$$;

CREATE TRIGGER tt_ioc_root_ins AFTER INSERT ON tt_ioc_root
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_ioc_cap();
CREATE TRIGGER tt_ioc_root_upd AFTER UPDATE ON tt_ioc_root
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_ioc_cap();

-- Seed with the plain INSERT path
INSERT INTO tt_ioc_root (a,b,c) VALUES (1,'one',1.1), (2,'two',2.2);

-- IOCDU mixing inserts and updates with varied varlena lengths to
-- catch format-mismatch corruption.
INSERT INTO tt_ioc_root (a,b,c) VALUES
  (1, repeat('A',60),  11.11),
  (3, repeat('B',80),  33.33),
  (2, repeat('C',100), 22.22),
  (4, repeat('D',150), 44.44)
ON CONFLICT (a) DO UPDATE SET b = excluded.b, c = excluded.c;

-- Verify final state: column values and lengths must line up exactly.
SELECT a, length(b) AS b_len, left(b,1) AS b_first, c FROM tt_ioc_root ORDER BY a;

-- Same statement with smaller read batches, so that rows are flushed from
-- more than one batch.  Output must match the default-batch-size run above.
SET yb_insert_on_conflict_read_batch_size = 1;
TRUNCATE tt_ioc_root;
INSERT INTO tt_ioc_root (a,b,c) VALUES (1,'one',1.1), (2,'two',2.2);
INSERT INTO tt_ioc_root (a,b,c) VALUES
  (1, repeat('A',60),  11.11),
  (3, repeat('B',80),  33.33),
  (2, repeat('C',100), 22.22),
  (4, repeat('D',150), 44.44)
ON CONFLICT (a) DO UPDATE SET b = excluded.b, c = excluded.c;
SELECT a, length(b) AS b_len, left(b,1) AS b_first, c FROM tt_ioc_root ORDER BY a;

SET yb_insert_on_conflict_read_batch_size = 3;
TRUNCATE tt_ioc_root;
INSERT INTO tt_ioc_root (a,b,c) VALUES (1,'one',1.1), (2,'two',2.2);
INSERT INTO tt_ioc_root (a,b,c) VALUES
  (1, repeat('A',60),  11.11),
  (3, repeat('B',80),  33.33),
  (2, repeat('C',100), 22.22),
  (4, repeat('D',150), 44.44)
ON CONFLICT (a) DO UPDATE SET b = excluded.b, c = excluded.c;
SELECT a, length(b) AS b_len, left(b,1) AS b_first, c FROM tt_ioc_root ORDER BY a;
RESET yb_insert_on_conflict_read_batch_size;

DROP TABLE tt_ioc_root;
DROP FUNCTION tt_ioc_cap();

----------------------------------------------------------------------
-- Section 14: Prepared statements
----------------------------------------------------------------------

-- Whether the wholerow junk column is needed is decided at plan time, so
-- cached plans need the same coverage as one-shot statements.  Each statement
-- runs with a custom plan and then with a generic plan, the generic plan more
-- than once so that it is reused.  Every EXECUTE fires once with only its own
-- rows, and 0-row EXECUTEs fire with empty transition tables.

CREATE TABLE tt_prep (id int PRIMARY KEY, val text, n int);

CREATE TRIGGER tt_prep_ins AFTER INSERT ON tt_prep
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert();
CREATE TRIGGER tt_prep_upd AFTER UPDATE ON tt_prep
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_update();
CREATE TRIGGER tt_prep_del AFTER DELETE ON tt_prep
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();

PREPARE tt_prep_ins_values(int) AS INSERT INTO tt_prep VALUES ($1, 'v', 0);
PREPARE tt_prep_ins_select(int, int) AS
  INSERT INTO tt_prep SELECT g, 's', 0 FROM generate_series($1, $2) g;
PREPARE tt_prep_ioc_nothing(int) AS
  INSERT INTO tt_prep VALUES ($1, 'dn', 0) ON CONFLICT DO NOTHING;
PREPARE tt_prep_ioc_update(int, int) AS
  INSERT INTO tt_prep VALUES ($1, 'du', 0), ($2, 'du', 0)
  ON CONFLICT (id) DO UPDATE SET val = excluded.val, n = tt_prep.n + 1;
PREPARE tt_prep_update(int, int) AS
  UPDATE tt_prep SET n = n + 10 WHERE id BETWEEN $1 AND $2;
PREPARE tt_prep_delete(int, int) AS
  DELETE FROM tt_prep WHERE id BETWEEN $1 AND $2;

SET plan_cache_mode = force_custom_plan;
EXECUTE tt_prep_ins_values(1);
EXECUTE tt_prep_ins_select(11, 12);
EXECUTE tt_prep_ins_select(2, 1);
EXECUTE tt_prep_ioc_nothing(1);
EXECUTE tt_prep_ioc_nothing(7);
EXECUTE tt_prep_ioc_update(1, 31);
EXECUTE tt_prep_update(11, 12);
EXECUTE tt_prep_update(90, 99);
EXECUTE tt_prep_delete(11, 12);
EXECUTE tt_prep_delete(90, 99);

SET plan_cache_mode = force_generic_plan;
EXECUTE tt_prep_ins_values(2);
EXECUTE tt_prep_ins_values(3);
EXECUTE tt_prep_ins_select(13, 14);
EXECUTE tt_prep_ins_select(15, 16);
EXECUTE tt_prep_ins_select(2, 1);
EXECUTE tt_prep_ioc_nothing(2);
EXECUTE tt_prep_ioc_nothing(8);
EXECUTE tt_prep_ioc_update(2, 32);
EXECUTE tt_prep_ioc_update(3, 33);
EXECUTE tt_prep_update(13, 14);
EXECUTE tt_prep_update(15, 16);
EXECUTE tt_prep_update(90, 99);
EXECUTE tt_prep_delete(13, 14);
EXECUTE tt_prep_delete(15, 16);
EXECUTE tt_prep_delete(90, 99);

SELECT name, generic_plans, custom_plans FROM pg_prepared_statements
  ORDER BY name;
SELECT * FROM tt_prep ORDER BY id;
DEALLOCATE ALL;
DROP TABLE tt_prep;

-- Trigger created after PREPARE: the cached generic plan was built without
-- the wholerow column, so it must be replanned once the trigger exists.  OLD
-- and NEW must include the non-PK columns.
CREATE TABLE tt_late (id int PRIMARY KEY, a text, b int, c text);
INSERT INTO tt_late SELECT g, 'a' || g, g, 'c' || g FROM generate_series(1, 6) g;

PREPARE tt_late_update(int) AS UPDATE tt_late SET b = b + 100 WHERE id = $1;
PREPARE tt_late_delete(int) AS DELETE FROM tt_late WHERE id = $1;
EXECUTE tt_late_update(1);
EXECUTE tt_late_delete(2);
CREATE TRIGGER tt_late_upd AFTER UPDATE ON tt_late
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_update();
CREATE TRIGGER tt_late_del AFTER DELETE ON tt_late
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();
EXECUTE tt_late_update(3);
EXECUTE tt_late_delete(4);
DROP TRIGGER tt_late_upd ON tt_late;
DROP TRIGGER tt_late_del ON tt_late;
EXECUTE tt_late_update(5);
EXECUTE tt_late_delete(6);
SELECT name, generic_plans, custom_plans FROM pg_prepared_statements
  ORDER BY name;
SELECT * FROM tt_late ORDER BY id;
DEALLOCATE ALL;

RESET plan_cache_mode;
DROP TABLE tt_late;

----------------------------------------------------------------------
-- Section 15: INSERT ... ON CONFLICT
----------------------------------------------------------------------

CREATE FUNCTION tt_ioc_ids() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'INSERT' THEN
    RAISE NOTICE 'INSERT new: %',
      (SELECT string_agg(format('%s:%s', id, val), ',' ORDER BY id)
         FROM new_table);
  ELSE
    RAISE NOTICE 'UPDATE old: %',
      (SELECT string_agg(format('%s:%s', id, val), ',' ORDER BY id)
         FROM old_table);
    RAISE NOTICE 'UPDATE new: %',
      (SELECT string_agg(format('%s:%s', id, val), ',' ORDER BY id)
         FROM new_table);
  END IF;
  RETURN NULL;
END;
$$;

CREATE TABLE tt_ioc (id int PRIMARY KEY, val text);
INSERT INTO tt_ioc SELECT g, 'old' FROM generate_series(1, 100) g;

CREATE TRIGGER tt_ioc_ins AFTER INSERT ON tt_ioc
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_ioc_ids();
CREATE TRIGGER tt_ioc_upd AFTER UPDATE ON tt_ioc
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_ioc_ids();

-- DO NOTHING: the INSERT trigger sees only ids 101..150, and the UPDATE
-- trigger does not fire.
INSERT INTO tt_ioc SELECT g, 'new' FROM generate_series(50, 150) g
  ON CONFLICT DO NOTHING;

-- DO UPDATE with 20 input rows, every third of which conflicts, across
-- read batch sizes (0 disables batching; 7 does not divide 20).  Each run
-- must produce the same output, with no id in both the INSERT trigger's NEW
-- and the UPDATE trigger's OLD/NEW.
TRUNCATE tt_ioc;
INSERT INTO tt_ioc SELECT g, 'old' FROM generate_series(3, 18, 3) g;
SET yb_insert_on_conflict_read_batch_size = 0;
INSERT INTO tt_ioc SELECT g, 'new' FROM generate_series(1, 20) g
  ON CONFLICT (id) DO UPDATE SET val = 'upd';

TRUNCATE tt_ioc;
INSERT INTO tt_ioc SELECT g, 'old' FROM generate_series(3, 18, 3) g;
SET yb_insert_on_conflict_read_batch_size = 1;
INSERT INTO tt_ioc SELECT g, 'new' FROM generate_series(1, 20) g
  ON CONFLICT (id) DO UPDATE SET val = 'upd';

TRUNCATE tt_ioc;
INSERT INTO tt_ioc SELECT g, 'old' FROM generate_series(3, 18, 3) g;
SET yb_insert_on_conflict_read_batch_size = 7;
INSERT INTO tt_ioc SELECT g, 'new' FROM generate_series(1, 20) g
  ON CONFLICT (id) DO UPDATE SET val = 'upd';

TRUNCATE tt_ioc;
INSERT INTO tt_ioc SELECT g, 'old' FROM generate_series(3, 18, 3) g;
RESET yb_insert_on_conflict_read_batch_size;
INSERT INTO tt_ioc SELECT g, 'new' FROM generate_series(1, 20) g
  ON CONFLICT (id) DO UPDATE SET val = 'upd';

DROP TABLE tt_ioc;
DROP FUNCTION tt_ioc_ids();

----------------------------------------------------------------------
-- Section 16: ATTACH / DETACH PARTITION
----------------------------------------------------------------------

-- Statement-level triggers fire only on the relation named in the DML, and
-- each relation's transition tables use that relation's own column order.
-- tt_ad_c has a dropped column and a different column order than tt_ad.
CREATE FUNCTION tt_log_ad() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF TG_OP = 'INSERT' THEN
    RAISE NOTICE '%: INSERT new=%', TG_TABLE_NAME,
      (SELECT string_agg(t::text, ', ' ORDER BY id) FROM new_table t);
  ELSIF TG_OP = 'UPDATE' THEN
    RAISE NOTICE '%: UPDATE old=%, new=%', TG_TABLE_NAME,
      (SELECT string_agg(t::text, ', ' ORDER BY id) FROM old_table t),
      (SELECT string_agg(t::text, ', ' ORDER BY id) FROM new_table t);
  ELSE
    RAISE NOTICE '%: DELETE old=%', TG_TABLE_NAME,
      (SELECT string_agg(t::text, ', ' ORDER BY id) FROM old_table t);
  END IF;
  RETURN NULL;
END;
$$;

CREATE TABLE tt_ad (id int, region text, val text, PRIMARY KEY (id, region))
  PARTITION BY LIST (region);
CREATE TABLE tt_ad_us PARTITION OF tt_ad FOR VALUES IN ('US');
CREATE TABLE tt_ad_eu PARTITION OF tt_ad FOR VALUES IN ('EU');
CREATE TABLE tt_ad_c (xdrop int, val text, region text, id int,
                      PRIMARY KEY (id, region));
ALTER TABLE tt_ad_c DROP COLUMN xdrop;

CREATE PROCEDURE tt_ad_add_triggers(r text) LANGUAGE plpgsql AS $$
BEGIN
  EXECUTE format('CREATE TRIGGER %I AFTER INSERT ON %I '
                 'REFERENCING NEW TABLE AS new_table '
                 'FOR EACH STATEMENT EXECUTE FUNCTION tt_log_ad()',
                 r || '_ins', r);
  EXECUTE format('CREATE TRIGGER %I AFTER UPDATE ON %I '
                 'REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table '
                 'FOR EACH STATEMENT EXECUTE FUNCTION tt_log_ad()',
                 r || '_upd', r);
  EXECUTE format('CREATE TRIGGER %I AFTER DELETE ON %I '
                 'REFERENCING OLD TABLE AS old_table '
                 'FOR EACH STATEMENT EXECUTE FUNCTION tt_log_ad()',
                 r || '_del', r);
END;
$$;

CALL tt_ad_add_triggers('tt_ad');
CALL tt_ad_add_triggers('tt_ad_us');
CALL tt_ad_add_triggers('tt_ad_eu');

-- As in PG, a table with any transition-table trigger (statement-level
-- included, despite the message) cannot be attached, so tt_ad_c gets its
-- triggers once it is a partition.
CALL tt_ad_add_triggers('tt_ad_c');
ALTER TABLE tt_ad ATTACH PARTITION tt_ad_c FOR VALUES IN ('APAC');
DROP TRIGGER tt_ad_c_ins ON tt_ad_c;
DROP TRIGGER tt_ad_c_upd ON tt_ad_c;
DROP TRIGGER tt_ad_c_del ON tt_ad_c;
ALTER TABLE tt_ad ATTACH PARTITION tt_ad_c FOR VALUES IN ('APAC');
CALL tt_ad_add_triggers('tt_ad_c');

-- DML on the parent: only tt_ad's triggers fire, with tt_ad_c's rows
-- converted to the parent's column order.
INSERT INTO tt_ad VALUES (1, 'US', 'us1'), (2, 'EU', 'eu2'), (3, 'APAC', 'ap3');
UPDATE tt_ad SET val = val || '_u';
-- Cross-partition move into tt_ad_c
UPDATE tt_ad SET region = 'APAC' WHERE id = 1;
DELETE FROM tt_ad WHERE id = 2;

-- DML on tt_ad_c: only tt_ad_c's triggers fire, in tt_ad_c's column order.
INSERT INTO tt_ad_c (id, region, val) VALUES (4, 'APAC', 'ap4');
UPDATE tt_ad_c SET val = val || '_c';
DELETE FROM tt_ad_c WHERE id = 4;

ALTER TABLE tt_ad DETACH PARTITION tt_ad_c;

-- DML on the parent no longer sees tt_ad_c's rows.
INSERT INTO tt_ad VALUES (5, 'US', 'us5'), (6, 'EU', 'eu6');
UPDATE tt_ad SET val = val || '_d';
DELETE FROM tt_ad;

-- DML on the detached tt_ad_c still fires only its own triggers.
INSERT INTO tt_ad_c (id, region, val) VALUES (7, 'APAC', 'ap7');
UPDATE tt_ad_c SET val = val || '_d';
DELETE FROM tt_ad_c;

DROP TABLE tt_ad, tt_ad_c;
DROP PROCEDURE tt_ad_add_triggers(text);
DROP FUNCTION tt_log_ad();

----------------------------------------------------------------------
-- Section 17: Single-row UPDATE / DELETE
----------------------------------------------------------------------

-- A single-row UPDATE/DELETE by primary key normally skips the scan and
-- writes directly (a Result plan with no scan under it).  Transition
-- triggers need the old row, so that fast path must be skipped.
CREATE TABLE tt_sr (id int PRIMARY KEY, a text, b int, c text);
INSERT INTO tt_sr SELECT g, 'a' || g, g, 'c' || g FROM generate_series(1, 8) g;

-- UPDATE with only a NEW TABLE trigger, DELETE with only an OLD TABLE trigger
CREATE TRIGGER tt_sr_upd AFTER UPDATE ON tt_sr
  REFERENCING NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_insert();
CREATE TRIGGER tt_sr_del AFTER DELETE ON tt_sr
  REFERENCING OLD TABLE AS old_table
  FOR EACH STATEMENT EXECUTE FUNCTION tt_log_delete();

EXPLAIN (COSTS OFF) UPDATE tt_sr SET b = 100 WHERE id = 1;
EXPLAIN (COSTS OFF) DELETE FROM tt_sr WHERE id = 2;

-- NEW must carry the unchanged columns; OLD must carry all non-PK columns.
UPDATE tt_sr SET b = 100 WHERE id = 1;
DELETE FROM tt_sr WHERE id = 2;

-- Parameterized form, with both custom and generic plans
PREPARE tt_sr_update(int) AS UPDATE tt_sr SET b = 100 WHERE id = $1;
PREPARE tt_sr_delete(int) AS DELETE FROM tt_sr WHERE id = $1;
SET plan_cache_mode = force_custom_plan;
EXPLAIN (COSTS OFF) EXECUTE tt_sr_update(3);
EXPLAIN (COSTS OFF) EXECUTE tt_sr_delete(4);
EXECUTE tt_sr_update(3);
EXECUTE tt_sr_delete(4);
SET plan_cache_mode = force_generic_plan;
EXPLAIN (COSTS OFF) EXECUTE tt_sr_update(5);
EXPLAIN (COSTS OFF) EXECUTE tt_sr_delete(6);
EXECUTE tt_sr_update(5);
EXECUTE tt_sr_delete(6);
RESET plan_cache_mode;

-- Without the triggers, the fast path is used again.
DROP TRIGGER tt_sr_upd ON tt_sr;
DROP TRIGGER tt_sr_del ON tt_sr;
EXPLAIN (COSTS OFF) UPDATE tt_sr SET b = 100 WHERE id = 7;
EXPLAIN (COSTS OFF) DELETE FROM tt_sr WHERE id = 8;
EXPLAIN (COSTS OFF) EXECUTE tt_sr_update(7);
EXPLAIN (COSTS OFF) EXECUTE tt_sr_delete(8);

SELECT * FROM tt_sr ORDER BY id;

DEALLOCATE ALL;
DROP TABLE tt_sr;

----------------------------------------------------------------------
-- Cleanup
----------------------------------------------------------------------

DROP TABLE tt_test;
DROP FUNCTION tt_log_insert();
DROP FUNCTION tt_log_update();
DROP FUNCTION tt_log_delete();
