# ALTER TABLE subcommands that take ShareUpdateExclusiveLock must not fail
# concurrent writers in an open transaction with a schema version mismatch
# (GH #34831).

setup
{
  CREATE TABLE r (id int PRIMARY KEY);
  INSERT INTO r VALUES (1), (2);
  CREATE TABLE t (id int PRIMARY KEY, rid int, v int);
  INSERT INTO t SELECT g, 1, g FROM generate_series(1, 30) g;
  ALTER TABLE t ADD CONSTRAINT t_v_pos CHECK (v > 0) NOT VALID;
  ALTER TABLE t ADD CONSTRAINT t_r_fk FOREIGN KEY (rid) REFERENCES r (id) NOT VALID;
  CREATE TABLE p (id int, v int) PARTITION BY RANGE (id);
  CREATE TABLE p1 PARTITION OF p FOR VALUES FROM (0) TO (1000);
  ALTER TABLE p ADD CONSTRAINT p_v_pos CHECK (v > 0) NOT VALID;
}

teardown
{
  DROP TABLE p;
  DROP TABLE t;
  DROP TABLE r;
}

session s1
step s1_begin		{ BEGIN; }
step s1_ins_t1		{ INSERT INTO t VALUES (100, 1, 100); }
step s1_ins_t2		{ INSERT INTO t VALUES (101, 2, 101); }
step s1_ins_p1		{ INSERT INTO p VALUES (100, 100); }
step s1_ins_p2		{ INSERT INTO p VALUES (101, 101); }
step s1_commit		{ COMMIT; }

session s2
step s2_begin		{ BEGIN; }
step s2_validate_check	{ ALTER TABLE t VALIDATE CONSTRAINT t_v_pos; }
step s2_validate_fk	{ ALTER TABLE t VALIDATE CONSTRAINT t_r_fk; }
step s2_set_stats	{ ALTER TABLE t ALTER COLUMN v SET STATISTICS 100; }
step s2_validate_part	{ ALTER TABLE p VALIDATE CONSTRAINT p_v_pos; }
step s2_commit		{ COMMIT; }

permutation s1_begin s1_ins_t1 s2_begin s2_validate_check s1_ins_t2 s2_commit s1_commit
permutation s1_begin s1_ins_t1 s2_begin s2_validate_fk s1_ins_t2 s2_commit s1_commit
permutation s1_begin s1_ins_t1 s2_begin s2_set_stats s1_ins_t2 s2_commit s1_commit
permutation s1_begin s1_ins_p1 s2_begin s2_validate_part s1_ins_p2 s2_commit s1_commit
