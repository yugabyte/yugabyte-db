-- ########## TIME HOURLY TESTS ##########
-- Additional tests: Testing nonsuperuser
\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true


-- NOTE: THIS FILE MUST BE RUN AS partman_basic AND CONNECT TO THE DATABASE THAT RAN PART 1 TO EFFECTIVLELY TEST AS NONSUPERUSER
--      Ex  pg_prove -ovf -U partman_basic -d mydb test/test_native/test_nonsuperuser/test-time-hourly-nonsuperuser-part2.sql

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(54);

CREATE TABLE partman_test.time_taptest_table (col1 serial, col2 text, col3 timestamp NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.time_taptest_undo (LIKE partman_test.time_taptest_table);

SELECT has_table('partman_test', 'time_taptest_table', 'Check that parent was created');

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'hourly');
ALTER TABLE partman.template_partman_test_time_taptest_table ADD PRIMARY KEY (col1);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');


INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

--SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 hour'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 hours'::interval);

SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_default', 'Check that default table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[25], 'Check count from time_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));

UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
-- New tables should have primary keys
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'9 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'9 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 hours'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside trigger scope goes to default');

-- Keep tables after undoing
SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.time_taptest_undo');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');


SELECT * FROM finish();
ROLLBACK;
