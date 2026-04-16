-- ########## ID TESTS ##########
-- Other tests: additional constraints multi column with update, start @ value of 100, undo_partition() full return of values

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(88);
CREATE SCHEMA partman_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.id_taptest_table (col1 int primary key, col2 text, col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(100,109), 'stuff'||generate_series(100,109));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table TO partman_basic;

SELECT create_parent('partman_test.id_taptest_table', 'col1', 'partman', '10');
-- Default optimize_constraint is 30, so set it equal to premake for when this test was originally written
UPDATE partman.part_config SET constraint_cols = '{"col2", "col3"}', optimize_constraint = 4 WHERE parent_table = 'partman_test.id_taptest_table';
SELECT has_table('partman_test', 'id_taptest_table_p100', 'Check id_taptest_table_p100 exists');
SELECT has_table('partman_test', 'id_taptest_table_p110', 'Check id_taptest_table_p110 exists');
SELECT has_table('partman_test', 'id_taptest_table_p120', 'Check id_taptest_table_p120 exists');
SELECT has_table('partman_test', 'id_taptest_table_p130', 'Check id_taptest_table_p130 exists');
SELECT has_table('partman_test', 'id_taptest_table_p140', 'Check id_taptest_table_p140 exists');
SELECT has_table('partman_test', 'id_taptest_table_p90', 'Check id_taptest_table_p90 exists');
SELECT has_table('partman_test', 'id_taptest_table_p80', 'Check id_taptest_table_p80 exists');
SELECT has_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 exists');
SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 doesn''t exists yet');
SELECT hasnt_table('partman_test', 'id_taptest_table_p150', 'Check id_taptest_table_p150 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p100', ARRAY['col1'], 'Check for primary key in id_taptest_table_p100');
SELECT col_is_pk('partman_test', 'id_taptest_table_p110', ARRAY['col1'], 'Check for primary key in id_taptest_table_p110');
SELECT col_is_pk('partman_test', 'id_taptest_table_p120', ARRAY['col1'], 'Check for primary key in id_taptest_table_p120');
SELECT col_is_pk('partman_test', 'id_taptest_table_p130', ARRAY['col1'], 'Check for primary key in id_taptest_table_p130');
SELECT col_is_pk('partman_test', 'id_taptest_table_p140', ARRAY['col1'], 'Check for primary key in id_taptest_table_p140');
SELECT col_is_pk('partman_test', 'id_taptest_table_p90', ARRAY['col1'], 'Check for primary key in id_taptest_table_p90');
SELECT col_is_pk('partman_test', 'id_taptest_table_p80', ARRAY['col1'], 'Check for primary key in id_taptest_table_p80');
SELECT col_is_pk('partman_test', 'id_taptest_table_p70', ARRAY['col1'], 'Check for primary key in id_taptest_table_p70');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_p100', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p100');
SELECT table_privs_are('partman_test', 'id_taptest_table_p110', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p110');
SELECT table_privs_are('partman_test', 'id_taptest_table_p120', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p120');
SELECT table_privs_are('partman_test', 'id_taptest_table_p130', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p130');
SELECT table_privs_are('partman_test', 'id_taptest_table_p140', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p140');
SELECT table_privs_are('partman_test', 'id_taptest_table_p90', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p90');
SELECT table_privs_are('partman_test', 'id_taptest_table_p80', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p80');
SELECT table_privs_are('partman_test', 'id_taptest_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p60');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p100', ARRAY[10], 'Check count from id_taptest_table_p100');

INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(60,99), 'stuff'||generate_series(60,99));
SELECT run_maintenance();
INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(110,145), 'stuff'||generate_series(110,145));
SELECT run_maintenance();

-- Check for additional constraints on text & date columns
SELECT col_has_check('partman_test', 'id_taptest_table_p60', 'col2', 'Check for additional constraint on col2 on id_taptest_table_p60');
SELECT col_has_check('partman_test', 'id_taptest_table_p60', 'col3', 'Check for additional constraint on col3 on id_taptest_table_p60');
SELECT col_has_check('partman_test', 'id_taptest_table_p70', 'col2', 'Check for additional constraint on col2 on id_taptest_table_p70');
SELECT col_has_check('partman_test', 'id_taptest_table_p70', 'col3', 'Check for additional constraint on col3 on id_taptest_table_p70');
SELECT col_has_check('partman_test', 'id_taptest_table_p80', 'col2', 'Check for additional constraint on col2 on id_taptest_table_p80');
SELECT col_has_check('partman_test', 'id_taptest_table_p80', 'col3', 'Check for additional constraint on col3 on id_taptest_table_p80');

SELECT has_table('partman_test', 'id_taptest_table_p150', 'Check id_taptest_table_p150 exists');
SELECT has_table('partman_test', 'id_taptest_table_p160', 'Check id_taptest_table_p160 exists');
SELECT has_table('partman_test', 'id_taptest_table_p170', 'Check id_taptest_table_p170 exists');
SELECT has_table('partman_test', 'id_taptest_table_p180', 'Check id_taptest_table_p180 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p190', 'Check id_taptest_table_p190 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p150', ARRAY['col1'], 'Check for primary key in id_taptest_table_p150');
SELECT col_is_pk('partman_test', 'id_taptest_table_p160', ARRAY['col1'], 'Check for primary key in id_taptest_table_p160');
SELECT col_is_pk('partman_test', 'id_taptest_table_p170', ARRAY['col1'], 'Check for primary key in id_taptest_table_p170');
SELECT col_is_pk('partman_test', 'id_taptest_table_p180', ARRAY['col1'], 'Check for primary key in id_taptest_table_p180');
SELECT table_privs_are('partman_test', 'id_taptest_table_p150', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p150');
SELECT table_privs_are('partman_test', 'id_taptest_table_p160', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p160');
SELECT table_privs_are('partman_test', 'id_taptest_table_p170', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p170');
SELECT table_privs_are('partman_test', 'id_taptest_table_p180', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p180');

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[86], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p60', ARRAY[10], 'Check count from id_taptest_table_p60');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p70', ARRAY[10], 'Check count from id_taptest_table_p70');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p80', ARRAY[10], 'Check count from id_taptest_table_p80');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p90', ARRAY[10], 'Check count from id_taptest_table_p90');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p100', ARRAY[10], 'Check count from id_taptest_table_p100');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p110', ARRAY[10], 'Check count from id_taptest_table_p110');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p120', ARRAY[10], 'Check count from id_taptest_table_p120');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p130', ARRAY[10], 'Check count from id_taptest_table_p130');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p140', ARRAY[6], 'Check count from id_taptest_table_p140');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p150', 'Check that next is empty');

-- pgtap is weird and converts all returned values to text unless specifically cast
SELECT results_eq('SELECT partitions_undone::int, rows_undone::int FROM undo_partition(''partman_test.id_taptest_table'', 20)'
    , $$VALUES (13, 86)$$
    , 'Check that undo_partition() returns correct values');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[86], 'Check count from parent table after undo');
SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p80', 'Check id_taptest_table_p80 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p90', 'Check id_taptest_table_p90 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p100', 'Check id_taptest_table_p100 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p110', 'Check id_taptest_table_p110 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p120', 'Check id_taptest_table_p120 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p130', 'Check id_taptest_table_p130 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p140', 'Check id_taptest_table_p140 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p150', 'Check id_taptest_table_p140 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p160', 'Check id_taptest_table_p140 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p170', 'Check id_taptest_table_p140 still exists');
SELECT has_table('partman_test', 'id_taptest_table_p180', 'Check id_taptest_table_p180 still exists');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p60', ARRAY[0], 'Check count from id_taptest_table_p60');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p70', ARRAY[0], 'Check count from id_taptest_table_p70');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p80', ARRAY[0], 'Check count from id_taptest_table_p80');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p90', ARRAY[0], 'Check count from id_taptest_table_p90');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p100', ARRAY[0], 'Check count from id_taptest_table_p100');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p110', ARRAY[0], 'Check count from id_taptest_table_p110');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p120', ARRAY[0], 'Check count from id_taptest_table_p120');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p130', ARRAY[0], 'Check count from id_taptest_table_p130');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p140', ARRAY[0], 'Check count from id_taptest_table_p140');

SELECT * FROM finish();
ROLLBACK;
