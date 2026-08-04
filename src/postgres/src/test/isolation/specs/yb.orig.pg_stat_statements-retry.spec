setup
{
    SELECT pg_stat_statements_reset();
    CREATE TABLE test (k int primary key, v int);
    INSERT INTO test VALUES (1, 1);
}
teardown
{
    DROP TABLE test;
}

session "s1"
setup { BEGIN; }
step "s1_update" { UPDATE test SET v=20 WHERE k=1; }
step "s1_commit" { COMMIT; }
step "s1_pgss_filter_retries" { SELECT query, calls, rows FROM pg_stat_statements WHERE total_retries > 0; }
step "s1_select_in_nonexistent_table" { SELECT * from nonexistent_table; }
step "s1_pgss_reset" { SELECT pg_stat_statements_reset(); }

session "s2"
setup { BEGIN; }
step "s2_update" { UPDATE test SET v=40 WHERE k=1; }
step "s2_commit" { COMMIT; }

# Check pgss conflict retry entry
permutation "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_pgss_filter_retries"

# Ensure pgss output is correct after resetting retries
permutation "s1_update" "s2_update" "s1_commit" "s2_commit" "s1_pgss_reset" "s1_update" "s1_pgss_filter_retries"

# Query errors out due to non-transaction conflict error
permutation "s1_select_in_nonexistent_table" "s1_commit" "s1_update" "s1_pgss_filter_retries"
