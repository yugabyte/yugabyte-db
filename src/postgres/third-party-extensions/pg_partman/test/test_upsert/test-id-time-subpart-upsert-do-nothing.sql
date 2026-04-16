-- ########## ID PARENT / TIME SUBPARENT TESTS ##########
-- Additional test: UPSERT DO NOTHING

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(261);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.id_taptest_table (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));
SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10', '{"col3"}', p_automatic_maintenance := 'on', p_jobmon := false, p_upsert := 'ON CONFLICT (col1) DO NOTHING');

SELECT has_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 exists');
SELECT has_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 exists');
SELECT has_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 exists');
SELECT has_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 exists');
SELECT has_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0', ARRAY['col1'], 'Check for primary key in id_taptest_table_p0');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10', ARRAY['col1'], 'Check for primary key in id_taptest_table_p10');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20', ARRAY['col1'], 'Check for primary key in id_taptest_table_p20');
SELECT col_is_pk('partman_test', 'id_taptest_table_p30', ARRAY['col1'], 'Check for primary key in id_taptest_table_p30');
SELECT col_is_pk('partman_test', 'id_taptest_table_p40', ARRAY['col1'], 'Check for primary key in id_taptest_table_p40');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table'')::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from id_taptest_table_p0');

-- Check that upsert doesn't throw conflict
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));
SELECT pass('Upsert DO NOTHING passed test'); 

-- Create subpartition
SELECT partman.create_sub_parent('partman_test.id_taptest_table', 'col3', 'partman', 'daily', p_upsert := 'ON CONFLICT (col1) DO NOTHING');
-- p0
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

-- Move data to new subpartitions
SELECT results_eq('SELECT partition_data_time(''partman_test.id_taptest_table_p0'', p_batch_count := 5)::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p0', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[9], 'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (should be empty)');

-- Check that upsert doesn't throw conflict
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));
SELECT pass('Upsert DO NOTHING passed test'); 

-- p10
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check that subparent table has had data moved to partition');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p10', 'Check count from parent table _p10 (should be empty)');

-- p20
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p20', 'Check that subparent table itself _p20 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p20', 'Check count from parent table _p20 (should be empty)');

-- p30
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p30', 'Check that subparent table itself _p30 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p30', 'Check count from parent table _p30 (should be empty)');

-- p40
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p40', 'Check that subparent table itself _p40 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p40', 'Check count from parent table _p40 (should be empty)');

-- insertion round 2
INSERT INTO partman_test.id_taptest_table (col1, col3) VALUES (generate_series(10,20), CURRENT_TIMESTAMP+'1 day'::interval);
SELECT run_maintenance();
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p0', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from parent table partman_test.id_taptest_table_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[9], 'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from parent table partman_test.id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p20', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[1], 'Check count from parent table partman_test.id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

-- Check that upsert doesn't throw conflict
INSERT INTO partman_test.id_taptest_table (col1, col3) VALUES (generate_series(10,20), CURRENT_TIMESTAMP+'1 day'::interval);
SELECT pass('Upsert DO NOTHING passed test (10-20)'); 

-- p50
SELECT has_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 exists');
SELECT col_is_pk('partman_test', 'id_taptest_table_p50', ARRAY['col1'], 'Check for primary key in id_taptest_table_p50');

SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p50', 'Check that subparent table itself _p50 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p50', 'Check count from parent table _p50 (should be empty)');

-- p60
SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 exists');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_p60');

SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p60', 'Check that subparent table itself _p60 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p60', 'Check count from parent table _p60 (should be empty)');

SELECT hasnt_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 doesn''t exists yet');

-- make sure new data is where it should be
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check count from only parent table _p10 (should be empty)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from subparent table id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[1], 'Check count from subparent table id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

-- Ensure time partitioning works for all sub partitions
UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table ~ 'partman_test.id_taptest_table_p' AND partition_type = 'partman';
SELECT run_maintenance();

-- Check for new time sub-partitions
-- Time data only exists for now() in _p0, so only one additional table will be created
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

-- Time data exists for now+1 day in p10, so 2 additional tables will be created
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));

-- Time data exists for now+1 day in p20, so 2 additional tables will be created
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));

-- No time data has been inserted for p30 and higher, so no more partitions should be created for them
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

-- Test dropping without retention set
SELECT drop_partition_time ('partman_test.id_taptest_table_p0', '2 days', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

UPDATE part_config SET retention = '10', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p0';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p10';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p20';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p30';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p40';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p50';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p60';

-- Test dropping with it set
SELECT run_maintenance();
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT undo_partition('partman_test.id_taptest_table_p10', 20, p_keep_table := false); 
SELECT undo_partition('partman_test.id_taptest_table_p20', 20, p_keep_table := false);
SELECT undo_partition('partman_test.id_taptest_table_p30', 20, p_keep_table := false);
SELECT undo_partition('partman_test.id_taptest_table_p40', 20, p_keep_table := false);
SELECT undo_partition('partman_test.id_taptest_table_p50', 20, p_keep_table := false);
SELECT undo_partition('partman_test.id_taptest_table_p60', 20, p_keep_table := false);

SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p0''', 
    'Check that partman_test.id_taptest_table_p0 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p10''', 
    'Check that partman_test.id_taptest_table_p10 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p20''', 
    'Check that partman_test.id_taptest_table_p20 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p30''', 
    'Check that partman_test.id_taptest_table_p30 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p40''', 
    'Check that partman_test.id_taptest_table_p40 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p50''', 
    'Check that partman_test.id_taptest_table_p50 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p60''', 
    'Check that partman_test.id_taptest_table_p60 was removed from part_config');

-- Check top parent is still empty
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that top parent table has not had any data moved to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from subparent table');
-- TODO Not returning result that pgtap can test. Unsure why.
--SELECT results_eq('SELECT undo_partition(''partman_test.id_taptest_table'', 20, p_keep_table := false);', ARRAY[11], 'Check undo partition_id returns correct number of rows');
SELECT undo_partition('partman_test.id_taptest_table', 20, p_keep_table := false);

SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 does not exist');

SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table''', 
    'Check that partman_test.id_taptest_table was removed from part_config');

SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[11], 'Check count from final unpartitioned table');

SELECT * FROM finish();
ROLLBACK;

