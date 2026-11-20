--
-- A command does not modify a row twice: like heap_update reporting
-- TM_SelfModified, a later attempt by the same command is ignored, and one
-- after a later command (a trigger or a function) modified the row fails.
--

-- UPDATE ... FROM and DELETE ... USING act on a target row once, even when it
-- joins several rows.
CREATE TABLE sm_target (k int PRIMARY KEY, v int UNIQUE, w int);
CREATE INDEX ON sm_target (w);
CREATE TABLE sm_src (k int, x int);
INSERT INTO sm_target VALUES (1, 10, 0), (2, 20, 0);
INSERT INTO sm_src VALUES (1, 1), (1, 2), (2, 3);

UPDATE sm_target t SET v = 7 + t.k FROM sm_src s WHERE t.k = s.k;
SELECT * FROM sm_target ORDER BY k;

WITH u AS (UPDATE sm_target t SET w = t.w + 1 FROM sm_src s
           WHERE t.k = s.k RETURNING t.*)
SELECT * FROM u ORDER BY k;

-- Matches that set an indexed column to different values leave one index
-- entry per row.
UPDATE sm_target t SET w = s.x FROM sm_src s WHERE t.k = s.k;
SELECT count(*) FROM sm_target;
SET enable_seqscan = off;
SELECT k FROM sm_target WHERE w BETWEEN 0 AND 100 ORDER BY k;
SELECT count(*) FROM sm_target WHERE w >= 0;
RESET enable_seqscan;

-- Primary key update.
UPDATE sm_target t SET k = t.k + 10 FROM sm_src s WHERE t.k = s.k;
SELECT k, v FROM sm_target ORDER BY k;

WITH d AS (DELETE FROM sm_target t USING sm_src s
           WHERE t.k = s.k + 10 RETURNING t.k, t.v)
SELECT * FROM d ORDER BY k;
SELECT count(*) FROM sm_target;

-- A parent and a child row with the same primary key are different rows.
CREATE TABLE sm_inh (k int PRIMARY KEY, v int);
CREATE TABLE sm_inh_child (PRIMARY KEY (k)) INHERITS (sm_inh);
INSERT INTO sm_inh VALUES (1, 0), (2, 0);
INSERT INTO sm_inh_child VALUES (1, 0);
WITH u AS (UPDATE sm_inh t SET v = t.v + 1 FROM sm_src s
           WHERE t.k = s.k RETURNING t.tableoid::regclass, t.*)
SELECT * FROM u ORDER BY tableoid::text, k;

-- Row triggers fire once per target row, and transition tables hold it once.
CREATE TABLE sm_trig (k int PRIMARY KEY, v int);
INSERT INTO sm_trig VALUES (1, 0);
CREATE FUNCTION sm_log() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE '% % %: % -> %', TG_WHEN, TG_LEVEL, TG_OP, OLD, NEW;
  RETURN NEW;
END $$;
CREATE TRIGGER sm_trig_before BEFORE UPDATE ON sm_trig
  FOR EACH ROW EXECUTE FUNCTION sm_log();
CREATE TRIGGER sm_trig_after AFTER UPDATE ON sm_trig
  FOR EACH ROW EXECUTE FUNCTION sm_log();
UPDATE sm_trig t SET v = t.v + 1 FROM (VALUES (1), (1)) s(k) WHERE t.k = s.k;
DROP TRIGGER sm_trig_before ON sm_trig;
DROP TRIGGER sm_trig_after ON sm_trig;

CREATE FUNCTION sm_log_transition() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  RAISE NOTICE 'old: %, new: %',
    (SELECT string_agg(o::text, ', ') FROM old_table o),
    (SELECT string_agg(n::text, ', ') FROM new_table n);
  RETURN NULL;
END $$;
CREATE TRIGGER sm_trig_transition AFTER UPDATE ON sm_trig
  REFERENCING OLD TABLE AS old_table NEW TABLE AS new_table
  FOR EACH STATEMENT EXECUTE FUNCTION sm_log_transition();
UPDATE sm_trig t SET v = t.v + 1 FROM (VALUES (1), (1)) s(k) WHERE t.k = s.k;
DROP TRIGGER sm_trig_transition ON sm_trig;

-- A match that a BEFORE ROW trigger skips does not modify the row, so a later
-- match still can.
CREATE FUNCTION sm_skip_large() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF NEW.v > 100 THEN
    RETURN NULL;
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER sm_trig_skip BEFORE UPDATE ON sm_trig
  FOR EACH ROW EXECUTE FUNCTION sm_skip_large();
UPDATE sm_trig t SET v = s.x FROM (VALUES (1, 500), (1, 5)) s(k, x) WHERE t.k = s.k;
SELECT * FROM sm_trig;
DROP TRIGGER sm_trig_skip ON sm_trig;

-- A cascading foreign key follows the single update.
CREATE TABLE sm_parent (k int PRIMARY KEY, v int UNIQUE);
CREATE TABLE sm_child (v int REFERENCES sm_parent (v) ON UPDATE CASCADE);
INSERT INTO sm_parent VALUES (10, 1);
INSERT INTO sm_child VALUES (1);
UPDATE sm_parent p SET v = s.v FROM (VALUES (10, 6), (10, 6)) s(k, v) WHERE p.k = s.k;
SELECT * FROM sm_parent;
SELECT * FROM sm_child;

-- Two writable CTEs updating the same row: the second one is ignored.
CREATE TABLE sm_cte (k int PRIMARY KEY, v int);
CREATE INDEX ON sm_cte (v);
INSERT INTO sm_cte VALUES (1, 0);
WITH a AS (UPDATE sm_cte SET v = 1 WHERE k = 1 RETURNING v),
     b AS (UPDATE sm_cte SET v = 2 WHERE k = 1 RETURNING v)
SELECT (SELECT count(*) FROM a) AS a, (SELECT count(*) FROM b) AS b;
SELECT * FROM sm_cte;
SET enable_seqscan = off;
SELECT k FROM sm_cte WHERE v = 1;
SELECT k FROM sm_cte WHERE v = 2;
RESET enable_seqscan;
WITH d AS (DELETE FROM sm_cte WHERE k = 1 RETURNING k)
UPDATE sm_cte SET v = 3 WHERE k = 1;
SELECT count(*) FROM sm_cte;

-- A BEFORE trigger that modifies the row being updated or deleted makes the
-- outer command fail.
CREATE TABLE sm_bt (k int PRIMARY KEY, v int);
INSERT INTO sm_bt VALUES (1, 0), (2, 0);
CREATE FUNCTION sm_bt_touch() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  UPDATE sm_bt SET v = v + 100 WHERE k = OLD.k;
  IF TG_OP = 'DELETE' THEN
    RETURN OLD;
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER sm_bt_before BEFORE UPDATE OR DELETE ON sm_bt
  FOR EACH ROW WHEN (pg_trigger_depth() = 0) EXECUTE FUNCTION sm_bt_touch();
UPDATE sm_bt SET v = 1 WHERE k = 1;
DELETE FROM sm_bt WHERE k = 1;
SELECT * FROM sm_bt ORDER BY k;
DROP TRIGGER sm_bt_before ON sm_bt;

-- So does a volatile function in the query that modifies the target row.
CREATE FUNCTION sm_bump(int) RETURNS int LANGUAGE sql VOLATILE AS
  $$ UPDATE sm_bt SET v = v + 1000 WHERE k = $1 RETURNING v $$;
UPDATE sm_bt SET v = sm_bump(k) WHERE k = 2;
SELECT * FROM sm_bt ORDER BY k;

-- A trigger modification rolled back to a savepoint does not count.
CREATE FUNCTION sm_bt_touch_and_fail() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  BEGIN
    UPDATE sm_bt SET v = v + 100 WHERE k = OLD.k;
    RAISE EXCEPTION 'undo';
  EXCEPTION WHEN raise_exception THEN
    NULL;
  END;
  RETURN NEW;
END $$;
CREATE TRIGGER sm_bt_before BEFORE UPDATE ON sm_bt
  FOR EACH ROW WHEN (pg_trigger_depth() = 0) EXECUTE FUNCTION sm_bt_touch_and_fail();
UPDATE sm_bt SET v = 5 WHERE k = 1;
SELECT * FROM sm_bt ORDER BY k;
DROP TRIGGER sm_bt_before ON sm_bt;

-- INSERT ... ON CONFLICT DO UPDATE cannot update a row that the same command
-- inserted or updated, with or without read batching.
CREATE TABLE sm_ioc (id int PRIMARY KEY, v int);
INSERT INTO sm_ioc VALUES (1, 0);
SET yb_insert_on_conflict_read_batch_size = 0;
INSERT INTO sm_ioc VALUES (2, 3), (2, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v;
INSERT INTO sm_ioc VALUES (1, 3), (1, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v;
-- The DO UPDATE does not happen, so the row is not updated twice.
INSERT INTO sm_ioc VALUES (1, 3), (1, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v
  WHERE sm_ioc.v > 100;
SELECT * FROM sm_ioc ORDER BY id;
SET yb_insert_on_conflict_read_batch_size = 1;
INSERT INTO sm_ioc VALUES (2, 3), (2, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v;
RESET yb_insert_on_conflict_read_batch_size;
INSERT INTO sm_ioc VALUES (2, 3), (2, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v;
INSERT INTO sm_ioc VALUES (1, 3), (1, 4) ON CONFLICT (id) DO UPDATE SET v = excluded.v;
SELECT * FROM sm_ioc ORDER BY id;

-- Each statement of a transaction can update the row again.
BEGIN;
UPDATE sm_ioc SET v = v + 1 WHERE id = 1;
UPDATE sm_ioc SET v = v + 1 WHERE id = 1;
INSERT INTO sm_ioc VALUES (1, 0) ON CONFLICT (id) DO UPDATE SET v = sm_ioc.v + 1;
SELECT * FROM sm_ioc WHERE id = 1;
COMMIT;

-- Single-row UPDATE and DELETE, which do not fetch the row, act on it once
-- too.  Writable CTEs that nothing references run after the main statement,
-- the later one first.
CREATE TABLE sm_single (k int PRIMARY KEY, v int, w int);
CREATE INDEX ON sm_single (w);
INSERT INTO sm_single VALUES (1, 0, 10), (2, 0, 20), (3, 0, 30), (4, 0, 40);
WITH a AS (UPDATE sm_single SET v = v + 1 WHERE k = 1)
UPDATE sm_single SET v = v + 10 WHERE k = 1;
WITH a AS (UPDATE sm_single SET v = v + 1 WHERE k = 2),
     b AS (UPDATE sm_single SET v = v + 10 WHERE k = 2)
SELECT 1;
-- The deleted row stays deleted.
WITH a AS (UPDATE sm_single SET v = 7 WHERE k = 3)
DELETE FROM sm_single WHERE k = 3;
-- The moved row is not left behind.
WITH a AS (UPDATE sm_single SET v = 7 WHERE k = 4)
UPDATE sm_single SET k = 14 WHERE k = 4;
SELECT * FROM sm_single ORDER BY k;
SET enable_seqscan = off;
SELECT k FROM sm_single WHERE w > 0 ORDER BY k;
RESET enable_seqscan;
-- A row the statement inserted is invisible to its single-row UPDATE.
WITH i AS (INSERT INTO sm_single VALUES (5, 0, 50))
UPDATE sm_single SET v = 99 WHERE k = 5;
SELECT * FROM sm_single WHERE k = 5;
-- A volatile function that updates the row being updated.
CREATE FUNCTION sm_single_bump(int) RETURNS int LANGUAGE sql VOLATILE AS
  $$ UPDATE sm_single SET v = v + 100 WHERE k = $1 RETURNING v $$;
UPDATE sm_single SET v = sm_single_bump(1) WHERE k = 1;
SELECT * FROM sm_single WHERE k = 1;

-- ON CONFLICT DO UPDATE cannot update a row that another node of the command
-- or a BEFORE trigger inserted.
WITH i AS (INSERT INTO sm_ioc VALUES (10, 0) RETURNING id)
INSERT INTO sm_ioc SELECT id, 1 FROM i ON CONFLICT (id) DO UPDATE SET v = 99;
WITH i AS (INSERT INTO sm_ioc VALUES (11, 0) ON CONFLICT DO NOTHING RETURNING id)
INSERT INTO sm_ioc SELECT id, 1 FROM i ON CONFLICT (id) DO UPDATE SET v = 99;
WITH c AS (INSERT INTO sm_ioc VALUES (12, 0) ON CONFLICT (id) DO UPDATE SET v = 99)
INSERT INTO sm_ioc VALUES (12, 0);
CREATE FUNCTION sm_ioc_pre() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  IF pg_trigger_depth() = 1 THEN
    INSERT INTO sm_ioc VALUES (NEW.id, -100);
  END IF;
  RETURN NEW;
END $$;
CREATE TRIGGER sm_ioc_pre BEFORE INSERT ON sm_ioc
  FOR EACH ROW EXECUTE FUNCTION sm_ioc_pre();
INSERT INTO sm_ioc VALUES (13, 1) ON CONFLICT (id) DO UPDATE SET v = 1;
INSERT INTO sm_ioc VALUES (14, 1) ON CONFLICT (id) DO NOTHING;
DROP TRIGGER sm_ioc_pre ON sm_ioc;
SELECT * FROM sm_ioc WHERE id >= 10 ORDER BY id;

-- A statement inserting into a partitioned table conflicts with the row
-- another node inserted into its partition.
CREATE TABLE sm_part (id int PRIMARY KEY, v int) PARTITION BY RANGE (id);
CREATE TABLE sm_part1 PARTITION OF sm_part FOR VALUES FROM (0) TO (10);
WITH i AS (INSERT INTO sm_part1 VALUES (1, 0) RETURNING id)
INSERT INTO sm_part SELECT id, 1 FROM i ON CONFLICT (id) DO UPDATE SET v = 99;

-- A row lock skips a row that its command or a later one modified.
CREATE TABLE sm_lock (k int PRIMARY KEY, v int);
INSERT INTO sm_lock SELECT g, 0 FROM generate_series(1, 30) g;
WITH u AS (UPDATE sm_lock SET v = 1 WHERE k = 1 RETURNING k)
SELECT t.k, t.v, (SELECT count(*) FROM u) AS updated FROM sm_lock t
  WHERE t.k = 1 FOR UPDATE OF t;
SELECT * FROM sm_lock WHERE k = 1;
CREATE FUNCTION sm_lock_loop() RETURNS int LANGUAGE plpgsql AS $$
DECLARE r record; n int := 0;
BEGIN
  FOR r IN SELECT k FROM sm_lock WHERE k <= 20 ORDER BY k FOR UPDATE LOOP
    UPDATE sm_lock SET v = v + 10 WHERE k = r.k + 1;
    n := n + 1;
  END LOOP;
  RETURN n;
END $$;
BEGIN;
SELECT sm_lock_loop();
SELECT k, v FROM sm_lock WHERE k BETWEEN 10 AND 13 ORDER BY k;
ROLLBACK;
BEGIN;
DECLARE sm_lock_cur CURSOR FOR SELECT k FROM sm_lock WHERE k <= 5 ORDER BY k FOR UPDATE;
FETCH sm_lock_cur;
UPDATE sm_lock SET v = 5 WHERE k = 2;
DELETE FROM sm_lock WHERE k = 3;
FETCH ALL FROM sm_lock_cur;
-- Rows modified before the cursor opened are locked.
DECLARE sm_lock_cur2 CURSOR FOR SELECT k FROM sm_lock WHERE k <= 5 ORDER BY k FOR UPDATE;
FETCH ALL FROM sm_lock_cur2;
COMMIT;

-- An UPDATE ... FROM whose target is the inner side of a join with repeated
-- matches updates each row once.
CREATE TABLE sm_small (k int PRIMARY KEY, v int);
CREATE TABLE sm_many (k int, x int);
INSERT INTO sm_small VALUES (1, 0), (2, 0);
INSERT INTO sm_many SELECT g % 2 + 1, g FROM generate_series(1, 1000) g;
ANALYZE sm_small, sm_many;
SET enable_mergejoin = off;
SET enable_nestloop = off;
EXPLAIN (COSTS OFF) UPDATE sm_small SET v = sm_small.v + 1 FROM sm_many
  WHERE sm_small.k = sm_many.k;
UPDATE sm_small SET v = sm_small.v + 1 FROM sm_many WHERE sm_small.k = sm_many.k;
SELECT * FROM sm_small ORDER BY k;
RESET enable_mergejoin;
RESET enable_nestloop;

DROP TABLE sm_target, sm_src, sm_inh, sm_trig, sm_parent, sm_child, sm_cte,
  sm_bt, sm_ioc, sm_single, sm_part, sm_lock, sm_small, sm_many CASCADE;
DROP FUNCTION sm_log(), sm_log_transition(), sm_skip_large(), sm_bt_touch(),
  sm_bump(int), sm_bt_touch_and_fail(), sm_single_bump(int), sm_ioc_pre(),
  sm_lock_loop();
