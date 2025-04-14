session s1
setup { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ; -- just to avoid a warning when setting the priority bound }
step s1_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_begin_rc { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1_commit { COMMIT; }
step s1_high_priority { SET yb_transaction_priority_lower_bound = 0.5; }
step s1_xact_lock { SELECT pg_advisory_xact_lock(1); }

session s2
setup { SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ; SET yb_max_query_layer_retries=100; }
step s2_begin_rr { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2_begin_rc { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s2_commit { COMMIT; }
step s2_low_priority { SET yb_transaction_priority_upper_bound = 0.4; }
step s2_xact_lock { SELECT pg_advisory_xact_lock(1); }
step s2_query_layer_retries_0 { SET yb_max_query_layer_retries=0; }

permutation s1_high_priority s2_low_priority s1_begin_rr s1_xact_lock s2_begin_rr s2_xact_lock s1_commit s2_commit
permutation s1_high_priority s2_low_priority s1_begin_rr s1_xact_lock s2_query_layer_retries_0 s2_begin_rr s2_xact_lock s1_commit s2_commit
permutation s1_begin_rc s1_xact_lock s2_query_layer_retries_0 s2_begin_rc s2_xact_lock s1_commit s2_commit