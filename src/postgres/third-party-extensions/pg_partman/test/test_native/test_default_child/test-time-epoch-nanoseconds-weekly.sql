-- ########## TIME WEEKLY EPOCH TESTS ##########
-- Other tests: combination of start_partition & constraint_cols/optimize_constraint. Requires manually running apply_constraint to set other old partitions
-- Test nanosecond epoch time partitioning

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(53);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;

CREATE TABLE partman_test.time_taptest_table (col1 bigint
    , col2 text
    , col3 bigint NOT NULL DEFAULT (extract('epoch' from CURRENT_TIMESTAMP)::bigint)*1000000000)
    PARTITION BY RANGE (col3);

CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'weekly', '{"col1"}', p_epoch := 'nanoseconds' 
    , p_premake := 2, p_start_partition := to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'YYYY-MM-DD HH24:MI:SS'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), (extract('epoch' from CURRENT_TIMESTAMP - '8 weeks'::interval)::bigint)*1000000000);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' exists (+1 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' exists (+2 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist (+3 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' exists (-1 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' exists (-2 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' exists (-3 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' exists (-4 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' exists (-5 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' exists (-6 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW')||' exists (-7 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW')||' exists (-8 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'9 weeks'::interval, 'IYYY"w"IW')||' does not exist (-9 weeks)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP - '8 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW')||' (-8 weeks)');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), (extract('epoch' from CURRENT_TIMESTAMP - '7 weeks'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), (extract('epoch' from CURRENT_TIMESTAMP - '6 weeks'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), (extract('epoch' from CURRENT_TIMESTAMP - '5 weeks'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), (extract('epoch' from CURRENT_TIMESTAMP - '4 week'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(38,49), (extract('epoch' from CURRENT_TIMESTAMP - '3 week'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), (extract('epoch' from CURRENT_TIMESTAMP - '2 weeks'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), (extract('epoch' from CURRENT_TIMESTAMP - '1 week'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), (extract('epoch' from CURRENT_TIMESTAMP + '1 week'::interval)::bigint)*1000000000);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,110), (extract('epoch' from CURRENT_TIMESTAMP + '2 weeks'::interval)::bigint)*1000000000);

SELECT partition_data_time('partman_test.time_taptest_table', 20);
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW')||' (-7 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' (-6 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' (-5 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[12], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 weeks)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 weeks)');

UPDATE part_config SET premake = 3, optimize_constraint = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();

-- Automatic run of apply_constraints due to run_maintenance will put constraint on "now() - 4 weeks" partition with an optimize_constraint value of 5
-- This is due to values "now() + 2 weeks" being inserted above.
-- Ex: Current week is 39. +2 weeks is 41. Going back 5 weeks is 36, but constraint is applied to the table OLDER than that value. Hence constraint will be on week 35
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 weeks)');
-- Must run apply_constraints() to manually set the other older constraints

SELECT apply_constraints('partman_test.time_taptest_table', 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'));
SELECT apply_constraints('partman_test.time_taptest_table', 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'));
SELECT apply_constraints('partman_test.time_taptest_table', 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'));
SELECT apply_constraints('partman_test.time_taptest_table', 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'));
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(111,120), (extract('epoch' from CURRENT_TIMESTAMP + '3 weeks'::interval)::bigint)*1000000000);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' exists (+3 weeks');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 weeks)');

UPDATE part_config SET premake = 4 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(121,130), (extract('epoch' from CURRENT_TIMESTAMP + '4 weeks'::interval)::bigint)*1000000000);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[130], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 weeks)');
SELECT col_has_check('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'col1'
    , 'Check for additional constraint on col1 on time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' exists (+4 weeks)');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), (extract('epoch' from CURRENT_TIMESTAMP + '20 weeks'::interval)::bigint)*1000000000);
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside trigger scope goes to default');

SELECT drop_partition_time('partman_test.time_taptest_table', '3 weeks', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' does not exist (-4 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' does not exist (-5 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' does not exist (-6 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW')||' does not exist (-7 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW')||' does not exist (-8 weeks)');

UPDATE part_config SET retention = '2 weeks'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' does not exist (-3 weeks)');
SELECT has_table('partman_retention_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' got moved to new schema (-3 weeks)');

SELECT undo_partition('partman_test.time_taptest_table', 20, p_keep_table := false, p_target_table := 'partman_test.undo_taptest');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.undo_taptest', ARRAY[92], 'Check count from parent table after undo');
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
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' does not exist (-1 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' does not exist (-2 weeks)');

SELECT * FROM finish();
ROLLBACK;

