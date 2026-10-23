# ALTER TABLE ... VALIDATE CONSTRAINT of a foreign key runs concurrently with
# DMLs on the referencing and referenced tables. Its check of the existing
# rows takes no row locks, so it must not wait for, or fail with a
# serialization error because of, a concurrent writer (GH #34643):
#
# 1-3. s2 holds an open DELETE of parent row 2, which cascades to child row
#      2, while s1 validates child_fk in READ COMMITTED or REPEATABLE READ.
#      VALIDATE finishes without waiting, and child_fk is valid whether s2
#      then commits or rolls back.
# 4.   VALIDATE fails when a child row has no parent row.
# 5.   Concurrent insert violating the FK constraint being validated fails;
#      VALIDATE succeeds
# 6.   Delete of a parent row referenced by the FK constraint being
#      validated fails at once, without waiting for VALIDATE
# 7.   With batched nested loop off, VALIDATE checks row by row and locks
#      each referenced parent row, so the same delete waits for VALIDATE
#
# VALIDATE of an FK constraint with concurrent valid DMLs is covered by
# alter-table-1.

setup
{
  CREATE TABLE parent (id int PRIMARY KEY);
  INSERT INTO parent VALUES (1), (2), (3);
  CREATE TABLE child (id int PRIMARY KEY, pid int);
  INSERT INTO child VALUES (1, 1), (2, 2);
  ALTER TABLE child ADD CONSTRAINT child_fk FOREIGN KEY (pid)
    REFERENCES parent (id) ON DELETE CASCADE NOT VALID;

  CREATE TABLE orphan (id int PRIMARY KEY, pid int);
  INSERT INTO orphan VALUES (1, 1), (2, 99);
  ALTER TABLE orphan ADD CONSTRAINT orphan_fk FOREIGN KEY (pid)
    REFERENCES parent (id) NOT VALID;

  CREATE TABLE noaction (id int PRIMARY KEY, pid int);
  INSERT INTO noaction VALUES (1, 3);
  ALTER TABLE noaction ADD CONSTRAINT noaction_fk FOREIGN KEY (pid)
    REFERENCES parent (id) NOT VALID;
}

teardown
{
  DROP TABLE child, orphan, noaction, parent;
}

session s1
step s1_begin			{ BEGIN; }
step s1_begin_rc		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_begin_rr		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_no_bnl			{ SET LOCAL yb_enable_batchednl = off; }
step s1_validate		{ ALTER TABLE child VALIDATE CONSTRAINT child_fk; }
step s1_validate_orphan	{ ALTER TABLE orphan VALIDATE CONSTRAINT orphan_fk; }
step s1_validate_noaction
{
  ALTER TABLE noaction VALIDATE CONSTRAINT noaction_fk;
}
step s1_commit			{ COMMIT; }
step s1_check
{
  SELECT conname, convalidated FROM pg_constraint
    WHERE conname IN ('child_fk', 'orphan_fk', 'noaction_fk')
    ORDER BY conname;
  SELECT * FROM child ORDER BY id;
  SELECT * FROM noaction ORDER BY id;
}

session s2
step s2_begin			{ BEGIN; }
step s2_delete			{ DELETE FROM parent WHERE id = 2; }
step s2_ins_noaction	{ INSERT INTO noaction VALUES (100, 3); }
step s2_ins_noaction_bad	{ INSERT INTO noaction VALUES (101, 99); }
step s2_del_referenced	{ DELETE FROM parent WHERE id = 3; }
step s2_commit			{ COMMIT; }
step s2_rollback		{ ROLLBACK; }

# 1.
permutation s2_begin s2_delete s1_begin_rc s1_validate s2_commit s1_commit
    s1_check

# 2.
permutation s2_begin s2_delete s1_begin_rr s1_validate s2_commit s1_commit
    s1_check

# 3.
permutation s2_begin s2_delete s1_begin_rc s1_validate s2_rollback s1_commit
    s1_check

# 4.
permutation s1_begin_rc s1_validate_orphan s1_commit s1_check

# 5.
permutation s2_begin s2_ins_noaction s1_begin s1_validate_noaction
    s2_ins_noaction_bad s1_commit s2_commit s1_check

# 6.
permutation s2_begin s1_begin s1_validate_noaction s2_del_referenced
    s1_commit s2_commit s1_check

# 7.
permutation s2_begin s1_begin s1_no_bnl s1_validate_noaction
    s2_del_referenced s1_commit s2_commit s1_check
