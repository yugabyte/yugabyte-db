\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(5);

-- YB: reduced number of rows by factor of 10 to prevent test timeout
SELECT has_table('partman_test', 'id_taptest_table_p110000', 'Check id_taptest_table_p110000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p120000', 'Check id_taptest_table_p120000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p130000', 'Check id_taptest_table_p130000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p140000', 'Check id_taptest_table_p140000 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p150000', 'Check id_taptest_table_p150000 doesn''t exists yet');

/* YB: default partition creation is disabled.
 * TODO(#3109): Re-enable it after transactional DDL support.
-- Test default partition
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(200000, 200009));

SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table WHERE col1 > 100000', ARRAY[10], 'Check count from id_taptest_table for out of band data');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_default', ARRAY[10], 'Check count from id_taptest_table_default');
*/ -- YB

SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.undo_partition_proc(''partman_test.id_taptest_table'', p_target_table := ''partman_test.id_taptest_table_target'', p_keep_table := false);".');
SELECT diag('!!! After that, run part4 of this script to check result and cleanup tests !!!');


SELECT * FROM finish();
