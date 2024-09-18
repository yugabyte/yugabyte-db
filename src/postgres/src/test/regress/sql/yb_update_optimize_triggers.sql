SET yb_fetch_row_limit TO 1024;
SET yb_explain_hide_non_deterministic_fields TO true;
SET yb_update_num_cols_to_compare TO 50;
SET yb_update_max_cols_size_to_compare TO 10240;

-- This test requires the t-server gflag 'ysql_skip_row_lock_for_update' to be set to false.

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
