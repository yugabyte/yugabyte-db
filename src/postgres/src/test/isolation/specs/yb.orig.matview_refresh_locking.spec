# Table-lock interaction of REFRESH MATERIALIZED VIEW with concurrent reads
# (GH #29273).
#
#   - REFRESH MATERIALIZED VIEW (non-concurrent) takes AccessExclusiveLock
#     and may run inside a transaction block: a concurrent plain SELECT must
#     block until the refreshing transaction finishes, and must then observe
#     the refreshed data.
#   - REFRESH MATERIALIZED VIEW CONCURRENTLY takes ExclusiveLock, which does
#     not conflict with AccessShare: it must NOT block against a plain
#     SELECT of an open transaction, and the reader's next statement in
#     READ COMMITTED must observe the refreshed data.

setup
{
  CREATE TABLE base (k int PRIMARY KEY, v int);
  INSERT INTO base VALUES (1, 1), (2, 2);
  CREATE MATERIALIZED VIEW mv AS SELECT k, v FROM base;
  CREATE UNIQUE INDEX mv_k_idx ON mv (k);
}

teardown
{
  DROP MATERIALIZED VIEW mv;
  DROP TABLE base;
}

session s1
step s1_begin		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_refresh		{ REFRESH MATERIALIZED VIEW mv; }
step s1_select_mv	{ SELECT * FROM mv ORDER BY k; }
step s1_commit		{ COMMIT; }

session s2
step s2_insert_base	{ INSERT INTO base VALUES (3, 3); }
step s2_refresh_conc	{ REFRESH MATERIALIZED VIEW CONCURRENTLY mv; }
step s2_select_mv	{ SELECT * FROM mv ORDER BY k; }

# Non-concurrent REFRESH holds AccessExclusiveLock: the concurrent SELECT
# blocks until the refreshing transaction commits, then sees the refreshed
# data.
permutation s2_insert_base s1_begin s1_refresh s2_select_mv s1_commit

# REFRESH CONCURRENTLY does not block against a plain SELECT of an open
# transaction; the reader's next statement (READ COMMITTED) observes the
# refreshed data.  It runs a long sequence of catalog operations, so it can
# outlast the assume-session-is-blocked heuristic without ever waiting.
permutation s2_insert_base s1_begin s1_select_mv s2_refresh_conc(yb_never_waits) s1_select_mv s1_commit
