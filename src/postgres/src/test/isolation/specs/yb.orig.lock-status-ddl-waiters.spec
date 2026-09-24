# Verify that pg_locks.ybdetails.blocked_by is populated for a waiting object
# lock when a DDL (ALTER TABLE) is blocked by a concurrent DML (INSERT).
#
# Requires object locking (enable_object_locking_for_table_locks). Registered
# in yb_isolation_object_locking_schedule.

setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int	PRIMARY KEY,
    v	int 	NOT NULL
  );

  INSERT INTO foo VALUES (1,1);
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup                       { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_insert"            { INSERT INTO foo VALUES (2,2); }
step "s1_lock_exclusive"    { LOCK TABLE foo IN ACCESS EXCLUSIVE MODE; }
step "s1_commit"            { COMMIT; }

session "s2"
step "s2_alter"             { ALTER TABLE foo ADD COLUMN v1 INT; }
step "s2_insert"            { INSERT INTO foo VALUES (3,3); }

session "s3"
step "s3_0secold"           { SET yb_locks_min_txn_age TO 0; }
# blocked_by is populated asynchronously once the waiter is registered. Poll
# until the waiting object lock's blocked_by references the granted holder.
step "s3_blocked_by"        {
    DO $$
    DECLARE
      found boolean := false;
      attempts int := 0;
    BEGIN
      LOOP
        SELECT EXISTS (
          SELECT 1 FROM pg_locks w
          JOIN pg_locks g
            ON g.granted
           AND g.relation = 'foo'::regclass
           AND g.locktype = 'relation'
           AND g.ybdetails->>'transactionid' IS NOT NULL
          WHERE NOT w.granted
            AND w.relation = 'foo'::regclass
            AND w.locktype = 'relation'
            AND w.ybdetails->'blocked_by' @> to_jsonb(g.ybdetails->>'transactionid')
        ) INTO found;
        EXIT WHEN found;
        attempts := attempts + 1;
        IF attempts > 300 THEN
          RAISE EXCEPTION 'timed out waiting for pg_locks.blocked_by to be populated';
        END IF;
        PERFORM pg_sleep(0.1);
      END LOOP;
    END$$;
}
# Same as s3_blocked_by, but require the waiter to be the non-global RowExclusiveLock
# (local object lock) blocked by a granted AccessExclusiveLock.
step "s3_blocked_by_nonglobal" {
    DO $$
    DECLARE
      found boolean := false;
      attempts int := 0;
    BEGIN
      LOOP
        SELECT EXISTS (
          SELECT 1 FROM pg_locks w
          JOIN pg_locks g
            ON g.granted
           AND g.relation = 'foo'::regclass
           AND g.locktype = 'relation'
           AND g.mode = 'AccessExclusiveLock'
           AND g.ybdetails->>'transactionid' IS NOT NULL
          WHERE NOT w.granted
            AND w.relation = 'foo'::regclass
            AND w.locktype = 'relation'
            AND w.mode = 'RowExclusiveLock'
            AND w.ybdetails->'blocked_by' @> to_jsonb(g.ybdetails->>'transactionid')
        ) INTO found;
        EXIT WHEN found;
        attempts := attempts + 1;
        IF attempts > 300 THEN
          RAISE EXCEPTION
            'timed out waiting for blocked_by on non-global RowExclusiveLock waiter';
        END IF;
        PERFORM pg_sleep(0.1);
      END LOOP;
    END$$;
}

# Global waiter: INSERT holds RowExclusiveLock; ALTER needs AccessExclusiveLock and waits.
permutation "s1_insert" "s2_alter" "s3_0secold" "s3_blocked_by" "s1_commit"

# Non-global waiter: ACCESS EXCLUSIVE blocks a local ROW EXCLUSIVE (INSERT).
permutation "s1_lock_exclusive" "s2_insert" "s3_0secold" "s3_blocked_by_nonglobal" "s1_commit"
