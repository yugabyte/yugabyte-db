-- ########## ID TESTS ##########
-- Test p_start_partition parameter

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(51);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;

CREATE TABLE partman_test.id_taptest_table (col1 int primary key, col2 text, col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(151,159));

SELECT create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10', p_start_partition := '150');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 does not exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p140', 'Check id_taptest_table_p140 does not exists');
SELECT has_table('partman_test', 'id_taptest_table_p150', 'Check id_taptest_table_p150 exists');
SELECT has_table('partman_test', 'id_taptest_table_p160', 'Check id_taptest_table_p160 exists');
SELECT has_table('partman_test', 'id_taptest_table_p170', 'Check id_taptest_table_p170 exists');
SELECT has_table('partman_test', 'id_taptest_table_p180', 'Check id_taptest_table_p180 exists');
SELECT has_table('partman_test', 'id_taptest_table_p190', 'Check id_taptest_table_p190 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p200', 'Check id_taptest_table_p200 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p150', ARRAY['col1'], 'Check for primary key in id_taptest_table_p150');
SELECT col_is_pk('partman_test', 'id_taptest_table_p160', ARRAY['col1'], 'Check for primary key in id_taptest_table_p160');
SELECT col_is_pk('partman_test', 'id_taptest_table_p170', ARRAY['col1'], 'Check for primary key in id_taptest_table_p170');
SELECT col_is_pk('partman_test', 'id_taptest_table_p180', ARRAY['col1'], 'Check for primary key in id_taptest_table_p180');
SELECT col_is_pk('partman_test', 'id_taptest_table_p190', ARRAY['col1'], 'Check for primary key in id_taptest_table_p190');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table'')::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p150', ARRAY[9], 'Check count from id_taptest_table_p150');

INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(160,175));
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p160', ARRAY[10], 'Check count from id_taptest_table_p160');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p170', ARRAY[6], 'Check count from id_taptest_table_p170');

SELECT has_table('partman_test', 'id_taptest_table_p200', 'Check id_taptest_table_p200 exists');
SELECT has_table('partman_test', 'id_taptest_table_p210', 'Check id_taptest_table_p210 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p220', 'Check id_taptest_table_p220 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p200', ARRAY['col1'], 'Check for primary key in id_taptest_table_p200');
SELECT col_is_pk('partman_test', 'id_taptest_table_p210', ARRAY['col1'], 'Check for primary key in id_taptest_table_p210');

INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(176,205));
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[55], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p170', ARRAY[10], 'Check count from id_taptest_table_p170');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p180', ARRAY[10], 'Check count from id_taptest_table_p180');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p190', ARRAY[10], 'Check count from id_taptest_table_p190');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p200', ARRAY[6], 'Check count from id_taptest_table_p200');

SELECT has_table('partman_test', 'id_taptest_table_p220', 'Check id_taptest_table_p220 exists');
SELECT has_table('partman_test', 'id_taptest_table_p230', 'Check id_taptest_table_p230 exists');
SELECT has_table('partman_test', 'id_taptest_table_p240', 'Check id_taptest_table_p240 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p250', 'Check id_taptest_table_p250 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p220', ARRAY['col1'], 'Check for primary key in id_taptest_table_p220');
SELECT col_is_pk('partman_test', 'id_taptest_table_p230', ARRAY['col1'], 'Check for primary key in id_taptest_table_p230');
SELECT col_is_pk('partman_test', 'id_taptest_table_p240', ARRAY['col1'], 'Check for primary key in id_taptest_table_p240');

INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(300,310));
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');
-- Remove rows in parent so drop function test can work properly
DELETE FROM ONLY partman_test.id_taptest_table;

SELECT drop_partition_id('partman_test.id_taptest_table', '45', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p150', 'Check id_taptest_table_p150 doesn''t exists anymore');

UPDATE part_config SET retention = '35' WHERE parent_table = 'partman_test.id_taptest_table';
SELECT drop_partition_id('partman_test.id_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'id_taptest_table_p160', 'Check id_taptest_table_p160 doesn''t exists anymore');
SELECT has_table('partman_retention_test', 'id_taptest_table_p160', 'Check id_taptest_table_p160 got moved to new schema');

SELECT undo_partition('partman_test.id_taptest_table', 10, p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[36], 'Check count from parent table after undo');
SELECT hasnt_table('partman_test', 'id_taptest_table_p170', 'Check id_taptest_table_p170 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p180', 'Check id_taptest_table_p180 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p190', 'Check id_taptest_table_p190 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p200', 'Check id_taptest_table_p200 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p210', 'Check id_taptest_table_p210 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p220', 'Check id_taptest_table_p220 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p230', 'Check id_taptest_table_p230 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p240', 'Check id_taptest_table_p240 doesn''t exists anymore');

SELECT * FROM finish();
ROLLBACK;
