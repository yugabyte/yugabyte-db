-- ########## TIME PARENT / ID SUBPARENT NATIVE TESTS ##########
-- Leaving pk/fk test out of this one so it can be used to test the native support in 11+

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(306);
CREATE SCHEMA partman_test;

-- Add back primary key when native supports it
--CREATE TABLE partman_test.time_taptest_table (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
CREATE TABLE partman_test.time_taptest_table (
    col1 int 
    , col2 text
    , col3 timestamptz NOT NULL DEFAULT now()) 
    PARTITION BY RANGE (col3);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'daily');
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

-- Restore back when native supports primary key
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
--SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
--    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));

-- Create subpartition 
SELECT partman.create_sub_parent('partman_test.time_taptest_table', 'col1', 'native', '20', p_native_check := 'yes');
--Reinsert data due to child table destruction
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check that subparent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', ARRAY[10], 
    'Check count from partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0');

SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT isnt_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' is not natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT is_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' is natively partitioned');
SELECT isnt_partitioned('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' is not natively partitioned');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p60', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p80', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0 does not exist');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||'_p0 does not exist');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,35), CURRENT_TIMESTAMP);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP - '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP - '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,35), CURRENT_TIMESTAMP - '4 days'::interval);
SELECT run_maintenance();

-- Check row counts from all levels
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has no data');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[315], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[35], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[16], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20');

-- Check that new serial child partition was created for each time sub-parent
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p100 exists');

UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();

-- Check for +5,+6,+7,+8,+9 days (since +4 days data exists) new subparent & children. Should only have 0,20,40,60,80 because subparent is empty and starting from zero
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||'_p0 does not exist');

-- insert batch 2
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,50), CURRENT_TIMESTAMP + '5 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP - '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP - '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(36,50), CURRENT_TIMESTAMP - '4 days'::interval);
SELECT run_maintenance();

-- All sub partition sets should be the same now
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p120', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120 exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120 exists');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p140', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p140', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p140 does not exist');

-- Check row counts from all levels
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has no data');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[500], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[50], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    ARRAY[19], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    ARRAY[20], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    ARRAY[11], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40');

-- Insert data outside of id subpartition scope, but still in top time partition scope
-- Re-add this when default partition is supported
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '1 day'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '2 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '3 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '4 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '5 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP - '1 day'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP - '2 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP - '3 days'::interval);
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP - '4 days'::interval);
--SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that top parent table has no data even tho sub-parents should now have some.');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
--    ARRAY[11], 'Check count from ONLY time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' (out of scope test)');

-- Insert data outside top time partition scope
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(211,220), CURRENT_TIMESTAMP - '20 days'::interval);
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[10], 'Check count from ONLY time_taptest_table (out of scope test)');
-- Remove above data so that retention can be tested
--DELETE FROM partman_test.time_taptest_table WHERE col1 >= 200 and col1 <= 220;

-- END section for testing default partition

-- Test dropping without retention set
SELECT drop_partition_id ('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), '20', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', 'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0 was dropped with direct drop call');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20', 'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20 was not dropped with direct drop call');

UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table';
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD');
UPDATE part_config SET retention = '25', retention_keep_table = false WHERE parent_table = 'partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD');

-- Test dropping with it set
SELECT drop_partition_id ('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0', 'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0 was dropped with direct drop call');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20', 'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20 was not dropped with direct drop call');

-- Test retention with run_maintenance()
SELECT run_maintenance();

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p100 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120 was dropped');

SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p0 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p20 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p40 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p60 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p80 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p100''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p100 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||'_p120 was removed from part_config');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p100 was dropped');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120 was dropped');


SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p0 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p20 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p40 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p60 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p80 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p100''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p100 was removed from part_config');
SELECT is_empty('SELECT parent_table FROM part_config WHERE parent_table = ''time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120''',
        'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||'_p120 was removed from part_config');

-- First undo all subpartitions
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p60', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p80', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0 does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p0 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p20 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p40 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p60', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p60 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p80', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p80 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'_p100 does not exist');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100', 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'_p100 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'_p100 exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p100', 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'_p100 exists');

SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||'''', 
    'Check that partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' was removed from part_config');

SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[0], 'Check that top parent is still empty');
-- Rows are missing data for col1 values 1-19 due to retention drop above
-- REDO this when default is supported to move data there
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD') );
--SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), ARRAY[31], 'Check data got move to partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD') );

SELECT results_eq('SELECT count(*)::int FROM partman_test.undo_taptest', ARRAY[248], 'Check count from target of undo_partition');

--Unable to undo_partition any further because col1 is duplicated in child tables, but parent key has it as a primary key. So done testing! :D

SELECT * FROM finish();
ROLLBACK;
