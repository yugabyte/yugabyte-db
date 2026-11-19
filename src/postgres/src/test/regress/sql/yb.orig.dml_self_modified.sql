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

DROP TABLE sm_target, sm_src, sm_inh, sm_trig, sm_parent, sm_child, sm_cte,
  sm_bt, sm_ioc CASCADE;
DROP FUNCTION sm_log(), sm_log_transition(), sm_skip_large(), sm_bt_touch(),
  sm_bump(int), sm_bt_touch_and_fail();
