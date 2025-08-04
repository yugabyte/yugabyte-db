--
-- NONCONCURRENT BACKFILL TESTS
--

-- Testing DDLs that perform nonconcurrent backfills. These need special handling
-- to ensure that we do not rerun the backfill step on the target.

-- Nonconcurrent indexes.
CREATE TABLE foo(i int PRIMARY KEY, j int);
INSERT INTO foo VALUES (1, 1), (2, 2), (3, 3);

CREATE INDEX NONCONCURRENTLY ON foo (j);
CREATE INDEX NONCONCURRENTLY nonconcurrent_foo ON foo (j);

-- Add unique constraint.
ALTER TABLE foo ADD CONSTRAINT unique_foo UNIQUE (j);

-- Partitioned indexes.
CREATE TABLE foo_partitioned_by_col(id int) PARTITION BY RANGE (id);
CREATE TABLE partition1 PARTITION OF foo_partitioned_by_col FOR VALUES FROM (1) TO (10);
CREATE TABLE partition2 PARTITION OF foo_partitioned_by_col FOR VALUES FROM (10) TO (20);
INSERT INTO foo_partitioned_by_col(id) VALUES (1), (2), (3);

CREATE UNIQUE INDEX partitioned_index ON foo_partitioned_by_col(id);

-- Also add a unique constraint on a partitioned table.
ALTER TABLE foo_partitioned_by_col ADD CONSTRAINT unique_foo_partitioned UNIQUE (id);

-- Perform a table rewrite, then ensure that another nonconcurrent backfill still works.
ALTER TABLE foo_partitioned_by_col ADD COLUMN created_at
        TIMESTAMP DEFAULT clock_timestamp() NOT NULL;

INSERT INTO foo_partitioned_by_col (id) VALUES (4);
CREATE INDEX ON foo_partitioned_by_col (created_at);
