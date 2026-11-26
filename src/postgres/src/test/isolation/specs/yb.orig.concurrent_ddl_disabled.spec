# With ysql_enable_concurrent_ddl off, the DDLs in a database that increment the
# catalog version run one at a time (#34673): each takes the database DDL lock
# and holds it until its transaction ends. The schedule runs with the flag off.
#
# In every permutation, holder runs ALTER TABLE a in a transaction block, which
# takes the lock, and the other sessions run DDLs on unrelated objects:
#   - a DDL that increments the catalog version waits until holder commits,
#     including after holder rolls back to a savepoint taken before its DDL,
#     and including a DDL in a procedure, called inside or outside a block;
#   - a DDL that does not increment the catalog version (CREATE TEMP TABLE)
#     and an ANALYZE from the auto-analyze service do not wait.
#
# TODO(#34673): whether ROLLBACK TO releases the lock taken in the rolled-back
# subtransaction is not part of the design. The savepoint permutation pins the
# current behavior, which may change.

setup
{
  CREATE TABLE a (k int PRIMARY KEY);
  CREATE TABLE b (k int PRIMARY KEY);
  CREATE TABLE c (k int PRIMARY KEY);
  CREATE PROCEDURE add_column(col text) LANGUAGE plpgsql AS $$
    BEGIN EXECUTE format('ALTER TABLE b ADD COLUMN %I int', col); END $$;
}

teardown
{
  DROP PROCEDURE add_column(text);
  DROP TABLE a, b, c;
}

session holder
step h_begin		{ BEGIN; }
step h_savepoint	{ SAVEPOINT s; }
step h_alter		{ ALTER TABLE a ADD COLUMN v int; }
step h_rollback_to	{ ROLLBACK TO s; }
step h_commit		{ COMMIT; }

session ddl
# A wait that the spec does not expect ends in this timeout, not in a hang.
setup			{ SET statement_timeout = '60s'; }
step d_alter		{ ALTER TABLE b ADD COLUMN v int; }
step d_temp		{ CREATE TEMP TABLE tt (k int); }
step d_analyze		{ ANALYZE b; }
step d_begin		{ BEGIN; }
step d_call		{ CALL add_column('c1'); }
step d_commit		{ COMMIT; }

session auto_analyze
# The auto-analyze service marks its connections with this GUC.
setup
{
  SET statement_timeout = '60s';
  SET yb_use_internal_auto_analyze_service_conn = true;
}
step aa_analyze		{ ANALYZE b; }
# Each table runs in its own transaction, and none of them takes the lock.
step aa_analyze_two	{ ANALYZE b, c; }

# A DDL on an unrelated table waits.
permutation h_begin h_alter d_alter h_commit

# The lock is held until the block ends, even after ROLLBACK TO.
permutation h_begin h_savepoint h_alter h_rollback_to d_alter h_commit

# CREATE TEMP TABLE does not wait; the ALTER after it does.
permutation h_begin h_alter d_temp d_alter h_commit

# An ANALYZE from the auto-analyze service does not wait; a user's ANALYZE does.
permutation h_begin h_alter aa_analyze aa_analyze_two d_analyze h_commit

# A DDL in a procedure waits, called outside a block (non-atomic) and inside one
# (atomic).
permutation h_begin h_alter d_call h_commit
permutation h_begin h_alter d_begin d_call h_commit d_commit
