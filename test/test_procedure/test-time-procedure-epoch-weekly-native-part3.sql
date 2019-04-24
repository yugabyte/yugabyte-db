\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(8);

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(111,120), extract('epoch' from CURRENT_TIMESTAMP + '3 weeks'::interval)::int);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' exists (+3 weeks');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.

-- Add back when index support is available
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 weeks)');

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 weeks)');

UPDATE part_config SET premake = 4 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(121,130), extract('epoch' from CURRENT_TIMESTAMP + '4 weeks'::interval)::int);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[130], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 weeks)');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' exists (+4 weeks)');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.

-- Add back when index support is available
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 weeks)');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), extract('epoch' from CURRENT_TIMESTAMP + '20 weeks'::interval)::int);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside partition scope goes to default');

SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.undo_partition_proc(''partman_test.time_taptest_table'', p_keep_table := false, p_wait := 0, p_target_table := ''partman_test.time_taptest_table_target'');".');
SELECT diag('!!! After that, run part4 of this script to check result and cleanup test objects !!!');


SELECT * FROM finish();

