# ALTER TABLE ... VALIDATE CONSTRAINT and SET STATISTICS run concurrently with
# DMLs in open transactions:
#
# 1. VALIDATE of a CHECK constraint with a concurrent valid insert
# 2. SET STATISTICS with a concurrent insert
# 3. VALIDATE of a CHECK constraint on a partitioned table with a concurrent
#    valid insert
# 4. Concurrent insert violating the CHECK constraint being validated fails;
#    VALIDATE succeeds
# 5. VALIDATE fails on a row that predates the NOT VALID constraint; the
#    concurrent writer is unaffected
# 6. A writer that cached the table before ADD ... NOT VALID waits for the ADD,
#    then its violating insert fails; VALIDATE succeeds
#
# yb.orig.validate_fk_constraint_concurrent_dml covers VALIDATE of an FK
# constraint.

setup
{
  CREATE TABLE t (id int PRIMARY KEY, v int);
  INSERT INTO t SELECT g, g FROM generate_series(1, 30) g;
  ALTER TABLE t ADD CONSTRAINT t_v_pos CHECK (v > 0) NOT VALID;
  CREATE TABLE tb (id int PRIMARY KEY, v int);
  INSERT INTO tb VALUES (1, -1);
  ALTER TABLE tb ADD CONSTRAINT tb_v_pos CHECK (v > 0) NOT VALID;
  CREATE TABLE tc (id int PRIMARY KEY, v int);
  INSERT INTO tc SELECT g, g FROM generate_series(1, 30) g;
  CREATE TABLE p (id int, v int) PARTITION BY RANGE (id);
  CREATE TABLE p1 PARTITION OF p FOR VALUES FROM (0) TO (1000);
  ALTER TABLE p ADD CONSTRAINT p_v_pos CHECK (v > 0) NOT VALID;
}

teardown
{
  DROP TABLE p;
  DROP TABLE tc;
  DROP TABLE tb;
  DROP TABLE t;
}

session s1
step s1_begin		{ BEGIN; }
step s1_ins_t1		{ INSERT INTO t VALUES (100, 100); }
step s1_ins_t2		{ INSERT INTO t VALUES (101, 101); }
step s1_ins_t_bad_check	{ INSERT INTO t VALUES (102, -1); }
step s1_ins_tb1		{ INSERT INTO tb VALUES (100, 100); }
step s1_ins_tb2		{ INSERT INTO tb VALUES (101, 101); }
step s1_ins_tc_valid	{ INSERT INTO tc VALUES (100, 100); }
step s1_ins_tc_bad	{ INSERT INTO tc VALUES (101, -1); }
step s1_ins_p1		{ INSERT INTO p VALUES (100, 100); }
step s1_ins_p2		{ INSERT INTO p VALUES (101, 101); }
step s1_commit		{ COMMIT; }

session s2
step s2_begin		{ BEGIN; }
step s2_validate_check	{ ALTER TABLE t VALIDATE CONSTRAINT t_v_pos; }
step s2_validate_tb	{ ALTER TABLE tb VALIDATE CONSTRAINT tb_v_pos; }
step s2_add_tc		{ ALTER TABLE tc ADD CONSTRAINT tc_v_pos CHECK (v > 0) NOT VALID; }
step s2_validate_tc	{ ALTER TABLE tc VALIDATE CONSTRAINT tc_v_pos; }
step s2_set_stats	{ ALTER TABLE t ALTER COLUMN v SET STATISTICS 100; }
step s2_validate_part	{ ALTER TABLE p VALIDATE CONSTRAINT p_v_pos; }
step s2_commit		{ COMMIT; }
step s2_show_tc		{
  SELECT conname, convalidated FROM pg_constraint
  WHERE conrelid = 'tc'::regclass ORDER BY conname;
  SELECT id, v FROM tc WHERE id >= 100 ORDER BY id;
}
step s2_show		{
  SELECT conname, convalidated FROM pg_constraint
  WHERE conrelid IN ('t'::regclass, 'tb'::regclass) ORDER BY conname;
  SELECT id, v FROM t WHERE id >= 100 ORDER BY id;
  SELECT id, v FROM tb ORDER BY id;
}

# 1.
permutation s1_begin s1_ins_t1 s2_begin s2_validate_check s1_ins_t2 s2_commit s1_commit

# 2.
permutation s1_begin s1_ins_t1 s2_begin s2_set_stats s1_ins_t2 s2_commit s1_commit

# 3.
permutation s1_begin s1_ins_p1 s2_begin s2_validate_part s1_ins_p2 s2_commit s1_commit

# 4.
permutation s1_begin s1_ins_t1 s2_begin s2_validate_check s1_ins_t_bad_check s2_commit s1_commit s2_show

# 5.
permutation s1_begin s1_ins_tb1 s2_begin s2_validate_tb s2_commit s1_ins_tb2 s1_commit s2_show

# 6.
permutation s1_ins_tc_valid s2_begin s2_add_tc s1_begin s1_ins_tc_bad s2_commit s2_validate_tc s1_commit s2_show_tc
