# ALTER TABLE subcommands that take ShareUpdateExclusiveLock must not fail
# concurrent writers in an open transaction with a schema version mismatch
# (GH #34831). VALIDATE CONSTRAINT of a foreign key alongside valid writes is
# covered by the ported alter-table-1 spec.
#
# NOT VALID constraints are still enforced on concurrent writes while the
# VALIDATE is in flight, and VALIDATE still fails on rows that predate the
# constraint.
#
# In legacy mode, a writer whose relcache predates ADD ... NOT VALID could
# insert a violating row unchecked, and only VALIDATE's schema version bump
# (which aborts active transactions) stopped it. With table locks the writer
# waits for the ADD and must see the constraint once it gets the lock.

setup
{
  CREATE TABLE r (id int PRIMARY KEY);
  INSERT INTO r VALUES (1), (2);
  CREATE TABLE t (id int PRIMARY KEY, rid int, v int);
  INSERT INTO t SELECT g, 1, g FROM generate_series(1, 30) g;
  ALTER TABLE t ADD CONSTRAINT t_v_pos CHECK (v > 0) NOT VALID;
  ALTER TABLE t ADD CONSTRAINT t_r_fk FOREIGN KEY (rid) REFERENCES r (id) NOT VALID;
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
  DROP TABLE r;
}

session s1
step s1_begin		{ BEGIN; }
step s1_ins_t1		{ INSERT INTO t VALUES (100, 1, 100); }
step s1_ins_t2		{ INSERT INTO t VALUES (101, 2, 101); }
step s1_ins_t_bad_check	{ INSERT INTO t VALUES (102, 1, -1); }
step s1_ins_t_bad_fk	{ INSERT INTO t VALUES (103, 99, 103); }
step s1_del_r_referenced	{ DELETE FROM r WHERE id = 1; }
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
step s2_validate_fk	{ ALTER TABLE t VALIDATE CONSTRAINT t_r_fk; }
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
  SELECT id, rid, v FROM t WHERE id >= 100 ORDER BY id;
  SELECT id, v FROM tb ORDER BY id;
}

permutation s1_begin s1_ins_t1 s2_begin s2_validate_check s1_ins_t2 s2_commit s1_commit
permutation s1_begin s1_ins_t1 s2_begin s2_set_stats s1_ins_t2 s2_commit s1_commit
permutation s1_begin s1_ins_p1 s2_begin s2_validate_part s1_ins_p2 s2_commit s1_commit

# A concurrent write that violates the constraint being validated still fails.
permutation s1_begin s1_ins_t1 s2_begin s2_validate_check s1_ins_t_bad_check s2_commit s1_commit s2_show
permutation s1_begin s1_ins_t1 s2_begin s2_validate_fk s1_ins_t_bad_fk s2_commit s1_commit s2_show
permutation s1_begin s2_begin s2_validate_fk s1_del_r_referenced s2_commit s1_commit s2_show

# VALIDATE fails on a row that predates the NOT VALID constraint, and the
# concurrent writer is unaffected by the failed VALIDATE.
permutation s1_begin s1_ins_tb1 s2_begin s2_validate_tb s2_commit s1_ins_tb2 s1_commit s2_show

# s1 caches tc without the constraint, then blocks behind the uncommitted
# ADD; once it gets the lock it must enforce the new constraint.
permutation s1_ins_tc_valid s2_begin s2_add_tc s1_begin s1_ins_tc_bad s2_commit s2_validate_tc s1_commit s2_show_tc
