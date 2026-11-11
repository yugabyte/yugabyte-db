-- YB: Gate the tests that need yb_pg_partman_check_native_txn_ddl_and_locks() to be true.
SELECT pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks()::int AS yb_use_txn_ddl_and_locks \gset
\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

\if :yb_use_txn_ddl_and_locks
SELECT plan(15);
\else \\ -- YB
SELECT plan(5); -- YB: decreased number of tests
\endif \\ -- YB

SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 weeks)');
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'));

\if :yb_use_txn_ddl_and_locks
-- Just testing normal undo function instead of procedure
SELECT undo_partition('partman_test.time_taptest_table', 20, p_keep_table := false, p_target_table := 'partman_test.target_time_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.target_time_taptest_table', ARRAY[110], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' does not exist (now)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' does not exist (+1 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' does not exist (+2 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' does not exist (+3 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' does not exist (+4 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist (+5 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' does not exist (+6 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' does not exist (-1 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' does not exist (-2 weeks)');
\endif \\ -- YB

DROP SCHEMA IF EXISTS partman_test CASCADE;

SELECT diag('!!! Final test complete !!!');

SELECT * FROM finish();
