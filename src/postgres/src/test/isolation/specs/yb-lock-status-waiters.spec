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
step "s1_select_for_update" { SELECT * FROM foo FOR UPDATE; }
step "s1_1secold"		        { SET yb_locks_min_txn_age TO 1; }
step "s1_wait2s"		        { SELECT pg_sleep(2); }
step "s1_lock_status"       {
    SELECT
        locktype, relation::regclass, mode, granted, fastpath, is_explicit,
        hash_cols, range_cols, attnum, column_id, multiple_rows_locked
    FROM yb_lock_status(null,null)
    ORDER BY
        relation::regclass::text, granted, hash_cols NULLS FIRST, range_cols NULLS FIRST;
}
step "s1_commit"            { COMMIT; }

session "s2"
step "s2_begin"             { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_insert"            { INSERT INTO foo VALUES (2,2); }
step "s2_update"            { UPDATE foo SET v=10 WHERE k=1; }

permutation "s1_insert" "s2_begin" "s2_insert" "s1_1secold" "s1_wait2s" "s1_lock_status" "s1_commit"
# TODO: uncomment the below permutation once #18195 is resolved
# permutation "s1_insert" "s2_insert" "s1_1secold" "s1_wait2s" "s1_lock_status" "s1_commit"
#
# TODO: uncomment this as soon as issue #18149 is resolved
# permutation "s1_select_for_update" "s2_update" "s1_1secold" "s1_wait2s" "s1_lock_status" "s1_commit"
