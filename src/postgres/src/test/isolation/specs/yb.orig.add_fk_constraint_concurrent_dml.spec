# ALTER TABLE ... ADD FOREIGN KEY on a table with rows runs concurrently with
# a DELETE on the referenced table (GH #34643). Its check of the existing rows
# takes no row locks, and no FK triggers exist yet to check concurrent
# writes, so only its ShareRowExclusiveLock keeps a concurrent DELETE from
# orphaning a child row that the check passed.
#
# 1-2. s2 holds an open DELETE of parent row 2 while s1 adds child_fk to a
#      child table with a row that references it. ADD FOREIGN KEY waits for
#      s2, then fails on the orphaned child row if s2 commits, and adds a
#      valid constraint if s2 rolls back.
# 3-4. s1 adds the FK first, and s2's DELETE of parent row 2 waits for s1 to
#      commit, then applies the new FK's delete action: NO ACTION fails the
#      DELETE, CASCADE deletes the child row too.

setup
{
  CREATE TABLE parent (id int PRIMARY KEY);
  INSERT INTO parent VALUES (1), (2);
  CREATE TABLE child (id int PRIMARY KEY, pid int);
  INSERT INTO child VALUES (1, 1), (2, 2);
}

teardown
{
  DROP TABLE child, parent;
}

session s1
step s1_begin_rc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_add_fk
{
  ALTER TABLE child ADD CONSTRAINT child_fk FOREIGN KEY (pid)
    REFERENCES parent (id);
}
step s1_add_fk_cascade
{
  ALTER TABLE child ADD CONSTRAINT child_fk FOREIGN KEY (pid)
    REFERENCES parent (id) ON DELETE CASCADE;
}
step s1_commit		{ COMMIT; }
step s1_check
{
  SELECT conname, convalidated FROM pg_constraint
    WHERE conname = 'child_fk';
}
step s1_check_rows
{
  SELECT * FROM parent ORDER BY id;
  SELECT * FROM child ORDER BY id;
}

session s2
step s2_begin		{ BEGIN; }
step s2_delete		{ DELETE FROM parent WHERE id = 2; }
step s2_commit		{ COMMIT; }
step s2_rollback	{ ROLLBACK; }

permutation s2_begin s2_delete s1_begin_rc s1_add_fk s2_commit s1_commit
    s1_check
permutation s2_begin s2_delete s1_begin_rc s1_add_fk s2_rollback s1_commit
    s1_check
permutation s1_begin_rc s1_add_fk s2_begin s2_delete s1_commit s2_commit
    s1_check_rows
permutation s1_begin_rc s1_add_fk_cascade s2_begin s2_delete s1_commit
    s2_commit s1_check_rows
