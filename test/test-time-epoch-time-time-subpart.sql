-- ########## TIME EPOCH PARENT / TIME EPOCH SUBPARENT / TIME EPOCH SUB-SUB-PARENT ##########
-- Currently tests 23, 39, 47 & 67 may fail around new years boundaries

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(72);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (col1 int primary key, col2 text, col3 bigint NOT NULL DEFAULT extract('epoch' from now()));
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), extract('epoch' from CURRENT_TIMESTAMP));

-- yearly
SELECT create_parent('partman_test.time_taptest_table', 'col3', 'partman', 'yearly', p_premake := 2, p_epoch := 'seconds');
-- Make sure optimize values can be different
UPDATE part_config SET optimize_trigger = 5, optimize_constraint = 10 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 years'::interval, 'YYYY')||' does not exist');

-- Move data from parent
SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that yearly parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));


-- monthly
SELECT create_sub_parent('partman_test.time_taptest_table', 'col3', 'partman', 'monthly', p_premake := 2, p_epoch := 'seconds');
-- Make sure optimize values can be different
UPDATE part_config_sub SET sub_optimize_trigger = 5, sub_optimize_constraint = 10, sub_retention_keep_table = false WHERE sub_parent = 'partman_test.time_taptest_table';
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'),
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM')||' exists');
-- Near end of year (Oct) following may fail since next hear's minimum month table will be created
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM')||' does not exist (this test may fail around year boundary. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM')||' does not exist');
-- Check that previous and future years had the minimal partition made
-- year +/- 1 tests may fail around year boundary. Tables may or may not exist depending on premake. That's fine. Should be ok for further in the future.
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_01 exists (this test may fail around year boundary. See comment in test code)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||'_02 does not exists (this test may fail around year boundary. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_02 does not exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01 exists (this test may fail around year boundary. See comment in test code)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_02 exists (this test may fail around year boundary. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 year'::interval, 'YYYY')||'_01 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 year'::interval, 'YYYY')||'_02 exists');

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||''')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved (yearly subparent)');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'Check data got moved out of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
-- Check subpart config
SELECT results_eq('SELECT sub_parent FROM part_config_sub ORDER BY sub_parent',
    ARRAY['partman_test.time_taptest_table'],
    'Check that part_config_sub has all tables configured as needed');

-- daily
SELECT results_eq('SELECT create_sub_parent(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP, ''YYYY''), ''col3'', ''partman'', ''daily'', p_premake := 2, p_epoch := ''seconds'')',
    ARRAY[true], 'Subpartitioning partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||' should return true');

SELECT results_eq('SELECT create_sub_parent(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP+''1 year''::interval, ''YYYY''), ''col3'', ''partman'', ''daily'', p_premake := 2, p_epoch := ''seconds'')',
    ARRAY[true], 'Subpartitioning partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||' should return true');
SELECT results_eq('SELECT create_sub_parent(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP+''2 years''::interval, ''YYYY''), ''col3'', ''partman'', ''daily'', p_premake := 2, p_epoch := ''seconds'')',
    ARRAY[true], 'Subpartitioning partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||' should return true');
SELECT results_eq('SELECT create_sub_parent(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP-''1 year''::interval, ''YYYY''), ''col3'', ''partman'', ''daily'', p_premake := 2, p_epoch := ''seconds'')',
    ARRAY[true], 'Subpartitioning partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||' should return true');
SELECT results_eq('SELECT create_sub_parent(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP-''2 years''::interval, ''YYYY''), ''col3'', ''partman'', ''daily'', p_premake := 2, p_epoch := ''seconds'')',
    ARRAY[true], 'Subpartitioning partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||' should return true');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'),
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||' exists')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exists');
-- This test may fail around the end of the year or the end of some months since the minimal partition for the next year or next month was created. That's fine
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist (this test may fail around year or month boundaries. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
-- Check that previous and future years had the minimal partition made
-- year +/- 1 tests may fail around year boundary. Tables may or may not exist depending on premake. That's fine. Should be ok for further in the future.
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_01 exists (this test may fail around year boundary. See comment in test code)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'1 years'::interval, 'YYYY')||'_01_02 does not exists (this test may fail around year boundary. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_01 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||'_01_02 does not exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_01 exists (this test may fail around year boundary. See comment in test code)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY')||'_01_02 exists (this test may fail around year boundary. See comment in test code)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_01 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_02', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP-'2 year'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY')||'_01_02 does not exists');

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||''')::int', 
    ARRAY[10], 'Check that partitioning function returns correct count of rows moved (monthly subparent)');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 
    'Check data got moved out of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
-- Check subpart config
SELECT results_eq('SELECT sub_parent FROM part_config_sub ORDER BY sub_parent',
    ARRAY['partman_test.time_taptest_table',
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')],
    'Check that part_config_sub has all tables configured as needed');

INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(11,20), 'stuff', extract('epoch' from CURRENT_TIMESTAMP+'1 day'::interval));
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check new data did not go into parent time_taptest_table');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY'), 
    'Check new data did not go into subparent time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY'));
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM'), 
    'Check new data did not go into subparent time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day', 'YYYY_MM_DD'));

UPDATE part_config SET premake = 3, optimize_trigger = 3 WHERE parent_table LIKE 'partman_test.time_taptest_table%' AND partition_type = 'partman';

SELECT run_maintenance();


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||' exists');
-- This test may fail if the next year's subpartition was already made and +3 months is into the next year, which may have been made by the condition that guarantees there is always at least 1 child table in a subpartition.
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM')||' exists. This test may fail in months near the end of the year.');
-- This test may fail if the next month's subpartition was already made and +3 days is after the 1st of the month, which may have been made by the condition that guarantees there is always at least 1 child table in a subpartition.
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists. This test may fail depending on the day of the month it is run.');
-- Check that future year had the minimal partition made
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01_01', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01_01 exists');

-- Check subpart config
SELECT results_eq('SELECT sub_parent FROM part_config_sub ORDER BY sub_parent',
     ARRAY['partman_test.time_taptest_table',
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 years'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 year'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'),
        'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')],
   'Check that part_config_sub has all tables configured as needed');

INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(21,30), 'stuff', extract('epoch' from CURRENT_TIMESTAMP+'3 years'::interval));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check new data did not go into parent time_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY'),
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'. Data should have gone here since monthly subpartition for it does not exist. This test may fail in January since that monthly partition should exist.');
-- Move data from yearly parent table and create appropriate monthly child for it
SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||''')::int', 
    ARRAY[10], 'Check that partitioning function returns correct count of rows moved from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'. This test may fail in January or the first of any month since that monthly/daily partition should exist.');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY'), 
    'Check new data did not go into subparent time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM'),
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM')||'. Data should have gone here since daily subpartition for it does not exist.');
-- Move data from monthly parent table and create appropriate daily child for it
SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM')||''')::int', 
    ARRAY[10], 'Check that partitioning function returns correct count of rows moved from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM')||'. This test may fail in January or the first of any month since that monthly/daily partition should exist.');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM'), 
    'Check new data did not go into subparent time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years', 'YYYY_MM_DD'));


/*
-- Disabled test for now. New years makes testing undo functions hard. Would be calling undo on year+1_01 twice and second one would fail.

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[30], 'Check count from top parent');

SELECT throws_ok('SELECT undo_partition_time(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP+''3 years''::interval, ''YYYY''), 20, p_keep_table := false)',
    'P0001',
    'Child table for this parent has child table(s) itself (partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||'_01). Run undo partitioning on this table or remove inheritance first to ensure all data is properly moved to parent
CONTEXT: SQL statement "SELECT undo_partition_time(''partman_test.time_taptest_table_p''||to_char(CURRENT_TIMESTAMP+''3 years''::interval, ''YYYY''), 20, p_keep_table := false)"
PL/pgSQL function throws_ok(text,character,text,text) line 16 at EXECUTE statement
DETAIL: 
HINT: ',
    'Check that undoing partitions is prevented if subpartitions still exist');

*/
SELECT * FROM finish();
ROLLBACK;
