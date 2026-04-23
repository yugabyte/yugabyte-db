# Tests that a session whose catalog cache is invalidated mid-statement by a concurrent DDL that
# records NO incremental invalidation messages still ends up with a consistent catalog instead of
# operating on a stale one.
#
# This exercises the full-invalidation fallback in YbMaybeRefreshCache (inval.c): when a catalog
# version bump has no usable inval messages, incremental refresh is impossible, so the cache must
# be fully blown away and the local catalog version advanced. Without that fallback, the waiting
# session below fails mid-statement with:
#   ERROR:  Table with identifier <id> not found: OBJECT_NOT_FOUND
#
# Mechanics:
# The materialized view's defining query embeds pg_sleep(5), so every REFRESH holds the
# AccessExclusiveLock on mv for ~5s. This is long enough that the YB isolation tester's heuristic
# (assume a step is blocked after 4s of being busy, see YB_NUM_SECONDS_TO_WAIT_TO_ASSUME_SESSION_BLOCKED
# in isolationtester.c) lets us issue the next session's step while the first REFRESH is still running.
#
# Sequence:
#  s2: SET yb_max_num_invalidation_messages = 0;  -- s2's DDL will record no inval messages
#  s2: REFRESH MATERIALIZED VIEW mv;  -- acquires AccessExclusiveLock, runs ~5s, commits with no inval msgs
#  s1: REFRESH MATERIALIZED VIEW mv;  -- blocks on s2's lock
#  s2 commits: bumps the catalog version with no usable inval messages
#  s1 acquires the lock and, lacking inval messages to reconcile, must fall back to a full cache
#     invalidation to pick up s2's new matview relfilenode rather than erroring on the stale one.
#
# NOTE on ordering: the session that set yb_max_num_invalidation_messages = 0 (s2) must REFRESH
# FIRST. It is the "producer" whose commit the second REFRESH reconciles against; only its commit
# records no inval messages. If s1 refreshed first instead, it would produce normal inval messages
# and s2 would reconcile incrementally, never reaching the fallback path under test.

setup
{
  CREATE TABLE base_t(k int PRIMARY KEY, v int);
  INSERT INTO base_t SELECT g, g FROM generate_series(1, 100) g;
  -- pg_sleep(5) makes every REFRESH hold the AccessExclusiveLock for ~5s.
  CREATE MATERIALIZED VIEW mv AS SELECT k, v FROM base_t, pg_sleep(5);
}

teardown
{
  DROP MATERIALIZED VIEW IF EXISTS mv;
  DROP TABLE IF EXISTS base_t;
}

session s1
step s1_refresh { REFRESH MATERIALIZED VIEW mv; }

session s2
step s2_set_inval_zero { SET yb_max_num_invalidation_messages = 0; }
step s2_refresh { REFRESH MATERIALIZED VIEW mv; }

permutation s2_set_inval_zero s2_refresh s1_refresh
