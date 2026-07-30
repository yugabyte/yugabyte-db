\set ON_ERROR_STOP on
------------------------------------------------
-- Test a composite (row type) column referencing a table whose column was later dropped.
-- The dropped column leaves a "........pg.dropped.N........" placeholder that import must tolerate.
------------------------------------------------
CREATE TABLE row_type_tbl (x4 INT, x1 INT);
INSERT INTO row_type_tbl (x4, x1) VALUES (8, 2);
CREATE TABLE row_type_user (x row_type_tbl);
INSERT INTO row_type_user (x) VALUES (ROW(9, 1));
ALTER TABLE row_type_tbl DROP COLUMN x4;

------------------------------------------------
-- Test a partitioned table where the child partition has a different physical column ordering
-- than the parent, plus dropped columns. The dropped columns end up at different attribute numbers
-- in the parent vs the child, producing mismatched pg.dropped placeholders that import must tolerate.
------------------------------------------------
CREATE TABLE part_drop_parent (a int, dummy int, dummy2 int, b int, c int) PARTITION BY RANGE (a);
CREATE TABLE part_drop_child (b int, c int, a int, dummy int, dummy2 int);
ALTER TABLE part_drop_parent ATTACH PARTITION part_drop_child FOR VALUES FROM (0) TO (5);
-- Insert a row while the to-be-dropped columns still exist.
INSERT INTO part_drop_parent (a, dummy, dummy2, b, c) VALUES (0, 999, 888, 5, 50);
ALTER TABLE part_drop_parent DROP COLUMN dummy;
ALTER TABLE part_drop_parent DROP COLUMN dummy2;

------------------------------------------------
-- Test partitioned tables used as composite (row type) columns, combined with dropped columns and
-- a child partition whose attribute ordering differs from the parent.
------------------------------------------------
CREATE TABLE comp_parent (a int, dummy int, dummy2 int, b int, c int) PARTITION BY RANGE (a);
CREATE TABLE comp_child (b int, c int, dummy int, dummy2 int, a int);
ALTER TABLE comp_parent ATTACH PARTITION comp_child FOR VALUES FROM (1) TO (9);
ALTER TABLE comp_parent DROP COLUMN dummy;
ALTER TABLE comp_parent DROP COLUMN dummy2;
CREATE TABLE comp_uses_parent (tag text, r comp_parent);
CREATE TABLE comp_uses_child (tag text, r comp_child);
INSERT INTO comp_parent (a, b, c) VALUES (4, 40, 400);
-- After dropping the columns, the comp_parent row type is (a, b, c) while the comp_child row type,
-- following its own attribute ordering, is (b, c, a).
INSERT INTO comp_uses_parent (tag, r) VALUES ('p1', ROW(1, 10, 100));
INSERT INTO comp_uses_child (tag, r) VALUES ('c1', ROW(20, 200, 2));
