\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(14);

SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_source', 'Check that source table has had data moved to partition');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 days'::interval, 'YYYY_MM_DD'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'));

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'10 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'10 day'::interval, 'YYYY_MM_DD')||' does not exist');

INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '4 days'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';


-- Test native default partition
INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '5 days'::interval);
SELECT results_eq ('SELECT count(*)::int FROM partman_test.time_taptest_table_default', ARRAY[1], 'Check native default child table for data');

-- Must either delete the data from default or move it to a temporary table in order to create next child table
-- Presence of a default partition will not allow a new child table with a range that contains data located in the default
DELETE FROM partman_test.time_taptest_table WHERE date_trunc('day', col3) = date_trunc('day', CURRENT_TIMESTAMP + '5 days'::interval);
SELECT is_empty ('SELECT col1 FROM partman_test.time_taptest_table_default', 'Check native default child table is empty');

SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.run_maintenance_proc();".');
SELECT diag('!!! After that, run part3 of this script to check result !!!');

SELECT * FROM finish();


