# VALIDATE CONSTRAINT must not fail with a serialization error when a
# concurrent transaction deletes a referenced row (GH #34643).
#
# s2 holds an open DELETE of parent row 2, which cascades to child row 2,
# while s1 validates child_fk. VALIDATE must finish without waiting on s2
# and mark child_fk valid, whether s2 then commits or rolls back.
#
# Not waiting must not mean not checking: a child row with no parent row
# still fails VALIDATE.

setup
{
  CREATE TABLE parent (id int PRIMARY KEY);
  CREATE TABLE child (id int PRIMARY KEY, pid int);
  INSERT INTO parent VALUES (1), (2);
  INSERT INTO child VALUES (1, 1), (2, 2);
  ALTER TABLE child ADD CONSTRAINT child_fk FOREIGN KEY (pid)
    REFERENCES parent (id) ON DELETE CASCADE NOT VALID;

  CREATE TABLE orphan (id int PRIMARY KEY, pid int);
  INSERT INTO orphan VALUES (1, 1), (2, 3);
  ALTER TABLE orphan ADD CONSTRAINT orphan_fk FOREIGN KEY (pid)
    REFERENCES parent (id) NOT VALID;
}

teardown
{
  DROP TABLE child, orphan, parent;
}

session s1
step s1_begin_rc		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_begin_rr		{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_validate		{ ALTER TABLE child VALIDATE CONSTRAINT child_fk; }
step s1_validate_orphan	{ ALTER TABLE orphan VALIDATE CONSTRAINT orphan_fk; }
step s1_commit			{ COMMIT; }
step s1_check
{
  SELECT conname, convalidated FROM pg_constraint
    WHERE conname IN ('child_fk', 'orphan_fk') ORDER BY conname;
  SELECT * FROM child ORDER BY id;
}

session s2
step s2_begin		{ BEGIN; }
step s2_delete		{ DELETE FROM parent WHERE id = 2; }
step s2_commit		{ COMMIT; }
step s2_rollback	{ ROLLBACK; }

permutation s2_begin s2_delete s1_begin_rc s1_validate s2_commit s1_commit s1_check
permutation s2_begin s2_delete s1_begin_rr s1_validate s2_commit s1_commit s1_check
permutation s2_begin s2_delete s1_begin_rc s1_validate s2_rollback s1_commit s1_check
permutation s1_begin_rc s1_validate_orphan s1_commit s1_check
