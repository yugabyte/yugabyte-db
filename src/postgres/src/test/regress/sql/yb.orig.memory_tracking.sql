--
-- Memory context allocators report block allocations to the PG MemTracker
-- (yb_heap_stats().PostgreSQL_memory_usage).
--

-- An unbounded sort keeps its tuples in a bump.c "Caller tuples" context.
CREATE TABLE memtrack (k int, v text);
INSERT INTO memtrack SELECT i, repeat('x', 1000) FROM generate_series(1, 10000) i;
SET work_mem = '64MB';

BEGIN;
SELECT "PostgreSQL_memory_usage" AS before_sort FROM yb_heap_stats() \gset
-- OFFSET 0 keeps the Sort below the narrow projection.
DECLARE c CURSOR FOR
  SELECT k, length(v) FROM (SELECT * FROM memtrack ORDER BY k OFFSET 0) s;
FETCH 1 FROM c;
-- ~10 MB of tuple data is held until the cursor is closed.
SELECT "PostgreSQL_memory_usage" - :before_sort > 8 * 1024 * 1024 AS sort_tracked
  FROM yb_heap_stats();
CLOSE c;
SELECT "PostgreSQL_memory_usage" - :before_sort < 2 * 1024 * 1024 AS sort_released
  FROM yb_heap_stats();
COMMIT;

DROP TABLE memtrack;
