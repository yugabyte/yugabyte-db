# ALTER TABLE on a partitioned root must lock the partitions it changes in
# DocDB before reading them (GH#34695).  Each permutation uses its own tree:
#   ac_*: ADD COLUMN while a sub-partition is being created under an
#         intermediate partition.  The root's lock doesn't conflict with that
#         CREATE, so the ALTER waits only once it locks ac_p_7, and the new
#         partition must get the column in DocDB as well as in pg_attribute.
#   dc_*: same, for DROP COLUMN of a primary-key column, which YB implements by
#         rewriting every partition; the new partition must be rewritten too.
#   ap_*: ADD PRIMARY KEY while a REPEATABLE READ transaction reads a partition
#         directly.  As in PG, the ALTER must wait for the reader, and the
#         reader must keep seeing the same rows.
#   fk_*: ADD FOREIGN KEY, which takes ShareRowExclusiveLock.  As in PG, it
#         doesn't wait for a REPEATABLE READ reader of a partition, and the
#         reader must keep working; it does wait for a writer to a partition,
#         and then fails validation on the row that writer committed.

setup
{
  CREATE TABLE ac_p (a int, b int, c text) PARTITION BY RANGE (a);
  CREATE TABLE ac_p_7 PARTITION OF ac_p FOR VALUES FROM (700) TO (800) PARTITION BY LIST (b);
  CREATE TABLE ac_p_7_b1 PARTITION OF ac_p_7 FOR VALUES IN (1);

  CREATE TABLE dc_p (a int, b int, c int, PRIMARY KEY (a, b, c)) PARTITION BY RANGE (a);
  CREATE TABLE dc_p_7 PARTITION OF dc_p FOR VALUES FROM (700) TO (800) PARTITION BY LIST (b);
  CREATE TABLE dc_p_7_b1 PARTITION OF dc_p_7 FOR VALUES IN (1);

  CREATE TABLE ap_p (a int NOT NULL, b int NOT NULL, c text) PARTITION BY RANGE (a);
  CREATE TABLE ap_p_7 PARTITION OF ap_p FOR VALUES FROM (700) TO (800) PARTITION BY LIST (b);
  CREATE TABLE ap_p_7_b1 PARTITION OF ap_p_7 FOR VALUES IN (1);
  INSERT INTO ap_p SELECT 700 + i, 1, 'r' FROM generate_series(0, 49) i;

  CREATE TABLE fk_ref (id int PRIMARY KEY);
  INSERT INTO fk_ref VALUES (1);
  CREATE TABLE fk_p (a int, b int, c int) PARTITION BY RANGE (a);
  CREATE TABLE fk_p_7 PARTITION OF fk_p FOR VALUES FROM (700) TO (800) PARTITION BY LIST (b);
  CREATE TABLE fk_p_7_b1 PARTITION OF fk_p_7 FOR VALUES IN (1);
  INSERT INTO fk_p SELECT 700 + i, 1, 1 FROM generate_series(0, 49) i;
}

teardown
{
  DROP TABLE IF EXISTS ac_p, dc_p, ap_p, fk_p, fk_ref;
}

session s1
step s1_begin			{ BEGIN; }
step s1_begin_rr		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_create_ac_part	{ CREATE TABLE ac_p_7_b0 PARTITION OF ac_p_7 FOR VALUES IN (0); }
step s1_create_dc_part	{ CREATE TABLE dc_p_7_b0 PARTITION OF dc_p_7 FOR VALUES IN (0); }
step s1_count			{ SELECT count(*) FROM ap_p_7_b1; }
step s1_count_fk		{ SELECT count(*) FROM fk_p_7_b1; }
step s1_insert_fk_bad	{ INSERT INTO fk_p_7_b1 VALUES (750, 1, 99); }
step s1_commit			{ COMMIT; }

session s2
step s2_add_column		{ ALTER TABLE ac_p ADD COLUMN x2 int DEFAULT 2; }
step s2_drop_pk_column	{ ALTER TABLE dc_p DROP COLUMN c; }
step s2_add_pk			{ ALTER TABLE ap_p ADD PRIMARY KEY (a, b); }
step s2_add_fk			{ ALTER TABLE fk_p ADD CONSTRAINT fk_c FOREIGN KEY (c) REFERENCES fk_ref (id); }

session s3
step s3_insert_ac		{ INSERT INTO ac_p VALUES (702, 0, 'r'), (703, 1, 'r'); }
step s3_select_ac		{ SELECT tableoid::regclass, * FROM ac_p ORDER BY a; }
step s3_insert_dc		{ INSERT INTO dc_p VALUES (702, 0), (703, 1); }
step s3_select_dc		{ SELECT tableoid::regclass, * FROM dc_p ORDER BY a; }
step s3_count			{ SELECT count(*) FROM ap_p_7_b1; }
step s3_insert_fk_bad	{ INSERT INTO fk_p_7_b1 VALUES (751, 1, 99); }
step s3_count_fk		{ SELECT count(*) FROM fk_p_7_b1; }

permutation s1_begin s1_create_ac_part s2_add_column s1_commit s3_insert_ac s3_select_ac
permutation s1_begin s1_create_dc_part s2_drop_pk_column s1_commit s3_insert_dc s3_select_dc
permutation s1_begin_rr s1_count s2_add_pk s1_count s1_commit s3_count
permutation s1_begin_rr s1_count_fk s2_add_fk s1_count_fk s1_commit s3_insert_fk_bad
permutation s1_begin s1_insert_fk_bad s2_add_fk s1_commit s3_count_fk
