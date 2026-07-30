------------------------------------------------
-- Test composite (row type) column referencing a table whose column was dropped
------------------------------------------------
SELECT * FROM row_type_tbl;
SELECT * FROM row_type_user;
\d row_type_tbl

------------------------------------------------
-- Test partitioned table with dropped columns and child attnum mismatch
------------------------------------------------
SELECT a, b, c FROM part_drop_parent ORDER BY a;
SELECT a, b, c FROM part_drop_child ORDER BY a;
-- New inserts should still be routed to the child partition
INSERT INTO part_drop_parent (a, b, c) VALUES (3, 30, 300);
SELECT a, b, c FROM part_drop_child ORDER BY a;
\d part_drop_parent
\d part_drop_child

------------------------------------------------
-- Test partitioned tables used as composite types with dropped columns
------------------------------------------------
SELECT a, b, c FROM comp_parent ORDER BY a;
SELECT tag, r::text FROM comp_uses_parent ORDER BY tag;
SELECT tag, r::text FROM comp_uses_child ORDER BY tag;
\d comp_parent
\d comp_child
