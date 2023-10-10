-- Create a partitioned hierarchy of LIST, RANGE and HASH.
CREATE TABLE root_list_parent (list_part_key char, hash_part_key int, range_part_key int) PARTITION BY LIST(list_part_key);
CREATE TABLE hash_parent PARTITION OF root_list_parent FOR VALUES in ('a', 'b') PARTITION BY HASH (hash_part_key);
CREATE TABLE range_parent PARTITION OF hash_parent FOR VALUES WITH (modulus 1, remainder 0) PARTITION BY RANGE (range_part_key);
CREATE TABLE child_partition PARTITION OF range_parent FOR VALUES FROM (1) TO (5);
INSERT INTO root_list_parent VALUES ('a', 1, 2);

-- Add a column to the parent table, verify that selecting data still works.
ALTER TABLE root_list_parent ADD COLUMN foo VARCHAR(2);
SELECT * FROM root_list_parent;

-- Alter column type at the parent table.
ALTER TABLE root_list_parent ALTER COLUMN foo TYPE VARCHAR(3);
INSERT INTO root_list_parent VALUES ('a', 1, 2, 'abc');
SELECT * FROM root_list_parent ORDER BY foo;

-- Drop a column from the parent table, verify that selecting data still works.
ALTER TABLE root_list_parent DROP COLUMN foo;
SELECT * FROM root_list_parent;

-- Retry adding a column after error.
ALTER TABLE root_list_parent ADD COLUMN foo text not null; -- fails due to not null constraint
ALTER TABLE root_list_parent ADD COLUMN foo text not null DEFAULT 'abc'; -- passes

-- Rename a column belonging to the parent table.
ALTER TABLE root_list_parent RENAME COLUMN list_part_key TO list_part_key_renamed;
SELECT * FROM child_partition ORDER BY foo;
TRUNCATE root_list_parent;

-- Add constraint to the parent table, verify that it reflects on the child partition.
ALTER TABLE root_list_parent ADD CONSTRAINT constraint_test UNIQUE (list_part_key_renamed, hash_part_key, range_part_key, foo);
INSERT INTO child_partition VALUES ('a', 1, 2), ('a', 1, 2);

-- Remove constraint from the parent table, verify that it reflects on the child partition.
ALTER TABLE root_list_parent DROP CONSTRAINT constraint_test;
INSERT INTO child_partition VALUES ('a', 1, 2), ('a', 1, 2);
SELECT * FROM root_list_parent ORDER BY foo;
