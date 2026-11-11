--
-- INSERT ON CONFLICT read batching when yb_insert_on_conflict_read_batch_size
-- changes during the statement.  The arrays that buffer a batch's rows are
-- allocated once per statement, so a change must not affect the running
-- statement.  Each case sets its own starting batch size, so this test does
-- not depend on the batch size the schedule runs with.
--

-- Return i.  When i = at, set the batch size to size until the end of the
-- transaction.
CREATE FUNCTION set_batch_size_at(i int, at int, size text) RETURNS int
LANGUAGE plpgsql AS $$
BEGIN
  IF i = at THEN
    PERFORM set_config('yb_insert_on_conflict_read_batch_size', size, true);
    RAISE NOTICE 'row %: batch size set to %', i,
      current_setting('yb_insert_on_conflict_read_batch_size');
  END IF;
  RETURN i;
END $$;

CREATE TABLE ioc_target (k int PRIMARY KEY, v int);

-- Lower the batch size from 1024 to 1 at row 2.
SET yb_insert_on_conflict_read_batch_size = 1024;
INSERT INTO ioc_target
  SELECT set_batch_size_at(g, 2, '1') FROM generate_series(1, 2000) g
  ON CONFLICT DO NOTHING;
SELECT count(*), min(k), max(k) FROM ioc_target;
TRUNCATE ioc_target;

-- Raise the batch size from 2 to 1000000 at row 2.
SET yb_insert_on_conflict_read_batch_size = 2;
INSERT INTO ioc_target
  SELECT set_batch_size_at(g, 2, '1000000') FROM generate_series(1, 2000) g
  ON CONFLICT DO NOTHING;
SELECT count(*), min(k), max(k) FROM ioc_target;
TRUNCATE ioc_target;

-- Set the batch size to 0 at row 1, before the first row is buffered.
SET yb_insert_on_conflict_read_batch_size = 1024;
INSERT INTO ioc_target
  SELECT set_batch_size_at(g, 1, '0') FROM generate_series(1, 2000) g
  ON CONFLICT DO NOTHING;
SELECT count(*), min(k), max(k) FROM ioc_target;
TRUNCATE ioc_target;

-- Raise the batch size from 2 to 1000 in DO UPDATE SET, while the first batch
-- is sent.
INSERT INTO ioc_target
  SELECT g,  -- 2000 int keys, i.e. 1.....2000
         0   -- all zero, i.e. 0
  FROM generate_series(1, 2000) g;
SET yb_insert_on_conflict_read_batch_size = 2;
INSERT INTO ioc_target
  SELECT g, 0 FROM generate_series(1, 2000) g
  ON CONFLICT (k) DO UPDATE SET v = set_batch_size_at(excluded.k, 1, '1000');
SELECT count(*) FROM ioc_target WHERE v = k;

-- Partitioned table: a partition decides whether to batch when its first row
-- arrives.  The batch size is 0 for rows 1..10 (first partition, no
-- batching), 1024 when row 11 reaches the second partition, and 1 from row 12.
CREATE TABLE ioc_target_part (k int PRIMARY KEY) PARTITION BY RANGE (k);
CREATE TABLE ioc_target_part_1 PARTITION OF ioc_target_part
  FOR VALUES FROM (1) TO (11);
CREATE TABLE ioc_target_part_2 PARTITION OF ioc_target_part
  FOR VALUES FROM (11) TO (MAXVALUE);
SET yb_insert_on_conflict_read_batch_size = 0;
INSERT INTO ioc_target_part
  SELECT set_batch_size_at(set_batch_size_at(g, 11, '1024'), 12, '1')
  FROM generate_series(1, 2000) g
  ON CONFLICT DO NOTHING;
SELECT count(*), min(k), max(k) FROM ioc_target_part;

RESET yb_insert_on_conflict_read_batch_size;
DROP TABLE ioc_target, ioc_target_part;
DROP FUNCTION set_batch_size_at;
