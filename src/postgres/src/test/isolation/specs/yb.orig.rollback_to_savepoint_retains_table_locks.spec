# ROLLBACK TO SAVEPOINT must only release the table locks acquired after the
# savepoint (GH #34734). Here s1's ROW EXCLUSIVE lock on t, taken before the
# savepoint, must keep blocking s2's ALTER TABLE ADD CONSTRAINT CHECK until s1
# commits, so that the constraint check sees s1's violating row.

setup
{
  CREATE TABLE t (k int PRIMARY KEY, v int);
  INSERT INTO t VALUES (1, 1);
  CREATE TABLE u (k int PRIMARY KEY);
}

teardown
{
  DROP TABLE t, u;
}

session s1
step s1_begin_rr	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_rc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_insert_t	{ INSERT INTO t VALUES (2, 20); }
step s1_savepoint	{ SAVEPOINT a; }
step s1_insert_u	{ INSERT INTO u VALUES (1); }
step s1_rollback_to	{ ROLLBACK TO SAVEPOINT a; }
step s1_commit		{ COMMIT; }

session s2
step s2_add_check	{ ALTER TABLE t ADD CONSTRAINT tc CHECK (v < 10); }
step s2_select		{ SELECT * FROM t ORDER BY k; }

permutation s1_begin_rr s1_insert_t s1_savepoint s1_insert_u s1_rollback_to s2_add_check s1_commit s2_select
permutation s1_begin_rc s1_insert_t s1_savepoint s1_insert_u s1_rollback_to s2_add_check s1_commit s2_select
