session s1
step s1_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_commit { COMMIT; }
step s1_rollback { ROLLBACK; }
step s1_savepoint_a { SAVEPOINT a; }
step s1_savepoint_b { SAVEPOINT b; }
step s1_release_savepoint_b { RELEASE SAVEPOINT b; }
step s1_rollback_to_savepoint_a { ROLLBACK TO SAVEPOINT a; }

step s1_create_table { CREATE TABLE test (k int primary key, v int); }
step s1_select { SELECT * FROM test; }
step s1_insert { INSERT INTO test VALUES (1, 1); }

step s1_xact_lock { SELECT pg_advisory_xact_lock(1); }
step s1_xact_lock_shared { SELECT pg_advisory_xact_lock_shared(1); }
step s1_xact_lock_bigint { SELECT pg_advisory_xact_lock(1, 1); }
step s1_xact_lock_bigint_shared { SELECT pg_advisory_xact_lock_shared(1, 1); }

# Session level locks
step s1_lock { SELECT pg_advisory_lock(1); }
step s1_lock_shared { SELECT pg_advisory_lock_shared(1); }
step s1_lock_bigint { SELECT pg_advisory_lock(1, 1); }
step s1_lock_bigint_shared { SELECT pg_advisory_lock_shared(1, 1); }
step s1_try_lock { SELECT pg_try_advisory_lock(1); }
step s1_try_lock_shared { SELECT pg_try_advisory_lock_shared(1); }
step s1_try_lock_bigint { SELECT pg_try_advisory_lock(1, 1); }
step s1_try_lock_bigint_shared { SELECT pg_try_advisory_lock_shared(1, 1); }

step s1_unlock { SELECT pg_advisory_unlock(1); }
step s1_unlock_shared { SELECT pg_advisory_unlock_shared(1); }
step s1_unlock_bigint { SELECT pg_advisory_unlock(1, 1); }
step s1_unlock_bigint_shared { SELECT pg_advisory_unlock_shared(1, 1); }
step s1_unlock_all { SELECT pg_advisory_unlock_all(); }

session s2
step s2_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2_commit { COMMIT; }
step s2_xact_lock { SELECT pg_advisory_xact_lock(1); }
step s2_xact_lock_shared { SELECT pg_advisory_xact_lock_shared(1); }
step s2_xact_lock_bigint { SELECT pg_advisory_xact_lock(1, 1); }
step s2_xact_lock_bigint_shared { SELECT pg_advisory_xact_lock_shared(1, 1); }
step s2_try_xact_lock { SELECT pg_try_advisory_xact_lock(1); }
step s2_try_xact_lock_shared { SELECT pg_try_advisory_xact_lock_shared(1); }
step s2_try_xact_lock_bigint { SELECT pg_try_advisory_xact_lock(1, 1); }
step s2_try_xact_lock_bigint_shared { SELECT pg_try_advisory_xact_lock_shared(1, 1); }

step s2_lock_bigint_shared { SELECT pg_advisory_lock_shared(1, 1); }
step s2_unlock_bigint_shared { SELECT pg_advisory_unlock_shared(1, 1); }

# Test the exclusive and shared mode conflict matrix.
permutation s1_begin_rr s1_xact_lock s2_begin_rr s2_xact_lock s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock s2_begin_rr s2_xact_lock_shared s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_bigint s2_begin_rr s2_xact_lock_bigint s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_bigint s2_begin_rr s2_xact_lock_bigint_shared s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_shared s2_begin_rr s2_xact_lock s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_shared s2_begin_rr s2_xact_lock_shared s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_bigint_shared s2_begin_rr s2_xact_lock_bigint s1_commit s2_commit
permutation s1_begin_rr s1_xact_lock_bigint_shared s2_begin_rr s2_xact_lock_bigint_shared s1_commit s2_commit

# pg_try_* calls don't wait for conflicting transactions to finish
permutation s1_begin_rr s1_xact_lock s2_begin_rr s2_try_xact_lock s2_commit s2_begin_rr s2_try_xact_lock_shared s2_commit s1_commit
permutation s1_begin_rr s1_xact_lock_bigint s2_begin_rr s2_try_xact_lock_bigint s2_commit s2_begin_rr s2_try_xact_lock_bigint_shared s2_commit s1_commit
permutation s1_begin_rr s1_xact_lock_shared s2_begin_rr s2_try_xact_lock s2_commit s2_begin_rr s2_try_xact_lock_shared s2_commit s1_commit
permutation s1_begin_rr s1_xact_lock_bigint_shared s2_begin_rr s2_try_xact_lock_bigint s2_commit s2_begin_rr s2_try_xact_lock_bigint_shared s2_commit s1_commit

# Session level locks
permutation s1_lock
permutation s1_lock_shared
permutation s1_lock_bigint
permutation s1_lock_bigint_shared
permutation s1_try_lock
permutation s1_try_lock_shared
permutation s1_try_lock_bigint
permutation s1_try_lock_bigint_shared
permutation s1_unlock
permutation s1_unlock_shared
permutation s1_unlock_bigint
permutation s1_unlock_bigint_shared
permutation s1_unlock_all

permutation s1_unlock s1_lock s1_unlock_shared s1_lock_shared s1_unlock_shared s1_unlock
permutation s1_lock_bigint_shared s2_lock_bigint_shared s1_lock_bigint s2_unlock_bigint_shared s1_unlock_bigint s1_unlock_bigint_shared

# Rolling back a transaction or a savepoint should release any advisory locks in that scope
permutation s1_begin_rr s1_xact_lock s2_begin_rr s2_xact_lock s1_rollback s2_commit
permutation s1_begin_rr s1_savepoint_a s1_savepoint_b s1_xact_lock s2_begin_rr s2_xact_lock s1_release_savepoint_b s1_rollback_to_savepoint_a s2_commit s1_commit

permutation s1_create_table s1_begin_rr s1_select s1_xact_lock s2_begin_rr s2_xact_lock s1_commit s2_commit
permutation s1_begin_rr s1_insert s1_xact_lock s2_begin_rr s2_xact_lock s1_commit s2_commit

# TODO: Add test for advisory locks on values from a table, i.e., of the form "select pg_advisory_xact_lock(c) from table1 for update"