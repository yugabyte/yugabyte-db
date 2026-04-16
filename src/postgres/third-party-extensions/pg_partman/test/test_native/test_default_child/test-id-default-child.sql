-- ########## ID DYNAMIC TESTS ##########
-- These tests only work on PG11+
-- Additional tests: Check that data can be moved from default to proper child tables
    -- Test using a pre-created template table and passing to create_parent. Should allow indexes to be made for initial children.

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(38);
CREATE SCHEMA partman_test;


CREATE TABLE partman_test.id_taptest_table (
    col1 bigint 
    , col2 text not null default 'stuff'
    , col3 timestamptz DEFAULT now()
    , col4 text) PARTITION BY RANGE (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.id_taptest_table INCLUDING ALL);
-- Template table
CREATE TABLE partman_test.template_id_taptest_table (LIKE partman_test.id_taptest_table);
-- Regular unique indexes do not work on native in PG11 if the partition key isn't included
CREATE UNIQUE INDEX ON partman_test.template_id_taptest_table (col4);

-- Create on template table
ALTER TABLE partman_test.template_id_taptest_table ADD PRIMARY KEY (col1);
CREATE INDEX ON partman_test.template_id_taptest_table (col3);

SELECT create_parent('partman_test.id_taptest_table', 'col1', 'native', '10', p_jobmon := false, p_start_partition := '3000000000', p_template_table := 'partman_test.template_id_taptest_table');

INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000001,3000000009), 'stuff'||generate_series(3000000001,3000000009));

SELECT has_table('partman_test', 'id_taptest_table_default', 'Check id_taptest_table_default exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000000', 'Check id_taptest_table_p3000000000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000010', 'Check id_taptest_table_p3000000010 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000020', 'Check id_taptest_table_p3000000020 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000030', 'Check id_taptest_table_p3000000030 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000040', 'Check id_taptest_table_p3000000040 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000000', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000000');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000010', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000010');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000020', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000020');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000030', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000030');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000040', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000040');
SELECT col_is_pk('partman_test', 'id_taptest_table_default', ARRAY['col1'], 'Check for primary key in id_taptest_table_default');

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000000', ARRAY[9], 'Check count from id_taptest_table_p3000000000');

SELECT run_maintenance();
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000010,3000000025), 'stuff'||generate_series(3000000010,3000000025));
-- Run again to make new partition based on latest data
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000010', ARRAY[10], 'Check count from id_taptest_table_p3000000010');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[6], 'Check count from id_taptest_table_p3000000020');

SELECT has_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000060', 'Check id_taptest_table_p3000000060 exists yet');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000050', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000050');

INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000026,3000000038), 'stuff'||generate_series(3000000026,3000000038));

SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[10], 'Check count from id_taptest_table_p3000000020');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000030', ARRAY[9], 'Check count from id_taptest_table_p3000000030');

SELECT has_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000080', 'Check id_taptest_table_p3000000080 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000060', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000060');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000070', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000070');

-- Requires default table option
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000200,3000000210), 'stuff'||generate_series(3000000200,3000000210));
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_default', ARRAY[11], 'Check that data with no specific child goes to default');

SELECT throws_ok('SELECT partman.partition_data_id(''partman_test.id_taptest_table'', 10, p_batch_interval := 2)', 
    'Custom intervals are not allowed when moving data out of the DEFAULT partition in a native set. Please leave p_interval/p_batch_interval parameters unset or NULL to allow use of partition set''s default partitioning interval.',
    'Check that interval cannot be set when partitioning off the default child');

-- Move data to proper child
SELECT partition_data_id('partman_test.id_taptest_table', 10);

SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_default', 'Check that data was removed from default.');
SELECT has_table('partman_test', 'id_taptest_table_p3000000200', 'Check id_taptest_table_p3000000200 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000210', 'Check id_taptest_table_p3000000210 exists');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000200', ARRAY[10], 'Check count from id_taptest_table_p3000000200 after moving from default');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000210', ARRAY[1], 'Check count from id_taptest_table_p3000000210 after moving from default');

SELECT * FROM finish();

ROLLBACK;
