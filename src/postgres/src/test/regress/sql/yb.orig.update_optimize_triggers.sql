SET yb_fetch_row_limit TO 1024;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

-- This test requires the t-server preview/auto flag 'ysql_yb_update_optimizations_infra' to be enabled.

CREATE OR REPLACE FUNCTION musical_chair() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  -- Increment a specifc column based on the modulo of the row supplied
  RAISE NOTICE 'Musical chairs invoked with h = %', NEW.h;
  IF OLD.h % 4 = 0 THEN
    NEW.h = NEW.h + 40;
  ELSIF OLD.h % 4 = 1 THEN
    NEW.v1 = NEW.v1 + 1;
  ELSIF OLD.h % 4 = 2 THEN
    NEW.v2 = NEW.v2 + 1;
  ELSE
    NEW.v3 = NEW.v3 + 1;
  END IF;
  RETURN NEW;
END;
$$;

-- CREATE a table that contains the columns specified in the above function.
DROP TABLE IF EXISTS mchairs_table;
CREATE TABLE mchairs_table (h INT PRIMARY KEY, v1 INT, v2 INT, v3 INT);
INSERT INTO mchairs_table (SELECT i, i, i, i FROM generate_series(1, 12) AS i);

-- Create some indexes to test the behavior of the updates
CREATE INDEX NONCONCURRENTLY mchairs_v1_v2 ON mchairs_table (v1 ASC, v2 DESC);
CREATE INDEX NONCONCURRENTLY mchairs_v3 ON mchairs_table (v3 HASH);

-- Add the trigger that plays musical chairs with the above table
CREATE TRIGGER mchairs_table_trigger BEFORE UPDATE ON mchairs_table FOR EACH ROW EXECUTE FUNCTION musical_chair();

-- The value of v1 should be incremented twice, index v1_v2 should be updated.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET v1 = v1 + 1 WHERE h = 1;
-- The value of v1 should be updated again, indexes v1_v2, v3 should be updated.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET h = h, v3 = v3 + 1 WHERE h = 1;
-- Multi-row scenario affecting 4 successive rows exactly with 4 flushes.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET h = h, v1 = v1, v2 = v3, v3 = v2 WHERE h > 8 AND h <= 12;

-- Validate the updates
SELECT * FROM mchairs_table ORDER BY h;

-- The decrement of v1 should be offset by the before row trigger. No indexes
-- should be updated.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET v1 = v1 - 1 WHERE h = 1;
-- Same as above but for the primary key
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET h = h - 40, v1 = v1 + 1 WHERE h = 4;
-- A subtle variation of the above to test the scenario that the decrement of
-- h is applied on the value of h that is returned by the before row trigger.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET h = h - 1, v1 = v1 + 1 WHERE h = 4;

-- Multi-row scenario.
EXPLAIN (ANALYZE, DIST, COSTS OFF) UPDATE mchairs_table SET v1 = v1 - 1, v2 = v2 - 1, v3 = v3 - 1 WHERE h > 4 AND h <= 8;

-- Again, validate the updates
SELECT * FROM mchairs_table ORDER BY h;

DROP TABLE mchairs_table;

---
--- Test to validate the behavior of after row triggers that are conditional on
--- specific columns (AFTER UPDATE OF <col-list>).
---
DROP TABLE IF EXISTS t_simple;
CREATE TABLE t_simple (h INT PRIMARY KEY, v1 INT, v2 INT, v3 INT, v4 INT);
CREATE INDEX NONCONCURRENTLY ON t_simple (v1);
CREATE INDEX NONCONCURRENTLY ON t_simple (v2, v3);
INSERT INTO t_simple (SELECT i, i, i, i, i FROM generate_series(1, 10) AS i);

CREATE OR REPLACE FUNCTION update_v1() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
	RAISE NOTICE 'update_v1 invoked with v1 = % where h = %', NEW.v1, NEW.h;
	NEW.v1 = NEW.v1 + 1;
	RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION notice_v1_update() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
	RAISE NOTICE 'New value of v1 = % where h = %', NEW.v1, NEW.h;
	RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION update_v2() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
	RAISE NOTICE 'update_v2 invoked with v2 = % where h = %', NEW.v2, NEW.h;
	NEW.v2 = NEW.v2 + 1;
	RETURN NEW;
END;
$$;

CREATE OR REPLACE FUNCTION notice_v2_update() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
	RAISE NOTICE 'New value of v2 = % where h = %',  NEW.v2, NEW.h;
	RETURN NEW;
END;
$$;

CREATE TRIGGER after_update_v1 AFTER UPDATE OF v1 ON t_simple FOR EACH ROW EXECUTE FUNCTION notice_v1_update();
CREATE TRIGGER after_update_v2 AFTER UPDATE OF v2 ON t_simple FOR EACH ROW EXECUTE FUNCTION notice_v2_update();

-- Appropriate AFTER ROW triggers fire when columns are mentioned in the SET clause.
UPDATE t_simple SET v1 = v1 + 1 WHERE h = 1;
UPDATE t_simple SET v1 = v1 + 1, v2 = v2 + 1 WHERE h = 2;
-- It shouldn't matter that the columns are unmodified by the query. If they are
-- in the set clause, appropriate triggers should fire.
UPDATE t_simple SET v2 = v2 WHERE h = 3;
UPDATE t_simple SET v1 = v2, v2 = v1 WHERE h = 4;

-- AFTER ROW triggers do not fire in any of the cases below.
UPDATE t_simple SET v3 = v3 + 1 WHERE h = 5;
UPDATE t_simple SET v4 = v1, v3 = v2 WHERE h = 6;
UPDATE t_simple SET h = h + 10 WHERE h > 9;

CREATE TRIGGER update_v1 BEFORE UPDATE ON t_simple FOR EACH ROW EXECUTE FUNCTION update_v1();
CREATE TRIGGER update_v2 BEFORE UPDATE ON t_simple FOR EACH ROW EXECUTE FUNCTION update_v2();
UPDATE t_simple SET v3 = v3 + 1 WHERE h = 7;
UPDATE t_simple SET v4 = v1 + 1 WHERE h = 8;
UPDATE t_simple SET h = h + 10, v4 = v4 + 1 WHERE h = 9;

-- AFTER ROW triggers should fire in the following cases even though the
-- BEFORE ROW triggers nullify the modification.
UPDATE t_simple SET v1 = v1 - 1 WHERE h = 1;
UPDATE t_simple SET v2 = 2 WHERE h = 2;
UPDATE t_simple SET v1 = v2 - 1, v2 = v1 - 1 WHERE h = 3;
UPDATE t_simple SET v1 = h, v2 = v1 WHERE h IN (4, 5, 6);

SELECT * FROM t_simple ORDER BY h;

---
--- Test to validate the behavior of statement triggers.
---
CREATE OR REPLACE FUNCTION update_statement_trigger() RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
	-- Should be all NULLs
	RAISE NOTICE 'statement trigger (%) invoked with row = %', TG_ARGV[0], NEW;
	RETURN NULL;
END;
$$;

CREATE TRIGGER before_update_statement BEFORE UPDATE ON t_simple FOR EACH STATEMENT EXECUTE FUNCTION update_statement_trigger('BEFORE');
-- No AFTER ROW triggers should fire.
UPDATE t_simple SET v3 = v3 - 1 WHERE h = 4;

-- Appropriate AFTER ROW triggers should fire.
CREATE TRIGGER after_update_statement_v1_v2 AFTER UPDATE OF v1, v2 ON t_simple FOR EACH STATEMENT EXECUTE FUNCTION update_statement_trigger('AFTER v1, v2');
UPDATE t_simple SET v1 = v1 + 1 WHERE h = 5;
UPDATE t_simple SET v1 = v2, v2 = v1 WHERE h = 6;
UPDATE t_simple SET v1 = 7, v2 = 7, v3 = 7 WHERE h = 7;
UPDATE t_simple SET h = h + 10, v4 = v4 + 1 WHERE h = 8;

-- Test the behavior of triggers when multiple rows are involved.
UPDATE t_simple SET v1 = v2 - 1, v2 = v3 - 1, v3 = v1 - 1 WHERE h > 7;

SELECT * FROM t_simple ORDER BY h;
