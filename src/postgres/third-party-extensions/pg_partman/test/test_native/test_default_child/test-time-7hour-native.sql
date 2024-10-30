-- ########## TIME 9 WEEKS TESTS ##########

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(61);

CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text default 'stuff', col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', '7 hours', p_date_trunc_interval := 'hour');

SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman', 'template_partman_test_time_taptest_table', 'Check that default template table was created');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS'), 'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+7 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'14 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+14 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'21 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+21 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'28 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+28 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+35 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (-7 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (-14 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (-21 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (-28 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'35 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (-35 hours)');


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table is empty. Should be impossible for native, but leaving test here just cause.');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('hour', CURRENT_TIMESTAMP) + '7 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('hour', CURRENT_TIMESTAMP) + '14 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('hour', CURRENT_TIMESTAMP) + '21 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), date_trunc('hour', CURRENT_TIMESTAMP) + '28 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), date_trunc('hour', CURRENT_TIMESTAMP) - '7 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), date_trunc('hour', CURRENT_TIMESTAMP) - '14 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), date_trunc('hour', CURRENT_TIMESTAMP) - '21 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), date_trunc('hour', CURRENT_TIMESTAMP) - '28 hours'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[7], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[21], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'));


UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';

-- Run to create proper future partitions
SELECT run_maintenance();
-- Insert after maintenance since native would put it into default 
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '35 hours'::interval);
-- Run again to create +5 partition now that data exists
SELECT run_maintenance();

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[22], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'));

-- Data exists for +35 weeks, with 5 premake so +70 week table should exist
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+35 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'42 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'42 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+42 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'49 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'49 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+49 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+56 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'63 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'63 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+63 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'70 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'70 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+70 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'77 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'77 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+77 hours)');

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '42 hours'::interval);
-- Run again now that +6 data exists
SELECT run_maintenance();

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'42 hours'::interval, 'YYYY_MM_DD_HH24MISS'), ARRAY[28], 'Check count from time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'42 hours'::interval, 'YYYY_MM_DD_HH24MISS'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'77 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'77 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+63 hours)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'84 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'84 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+84 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'91 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'91 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+91 hours)');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '200 hours'::interval);
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[159], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside existing child tables goes to default');

SELECT drop_partition_time('partman_test.time_taptest_table', '21 hours', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'28 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (-28 hours)');

UPDATE part_config SET retention = '14 hours' WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'21 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (-21 hours)');

SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM  partman_test.undo_taptest', ARRAY[129], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MISS')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+7 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+14 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'21 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+21 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'28 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+28 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'35 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+35 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'42 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+42 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'49 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+49 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+56 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'63 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+63 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'70 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+70 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'77 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+77 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'84 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+84 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'91 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+91 hours)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'14 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (-14 hours)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    'Check time_taptest_table_p'||to_char(date_trunc('hour',CURRENT_TIMESTAMP)-'7 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (-7 hours)');

SELECT hasnt_table('partman', 'template_partman_test_time_taptest_table', 'Check that template table was dropped');

SELECT * FROM finish();
ROLLBACK;

