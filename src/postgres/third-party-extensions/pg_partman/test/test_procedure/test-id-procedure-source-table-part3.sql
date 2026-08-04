\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(7);

SELECT has_table('partman_test', 'id_taptest_table_p1100000', 'Check id_taptest_table_p1100000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p1200000', 'Check id_taptest_table_p1200000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p1300000', 'Check id_taptest_table_p1300000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p1400000', 'Check id_taptest_table_p1400000 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1500000', 'Check id_taptest_table_p1500000 doesn''t exists yet');

-- Test default partition
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(2000000, 2000009));

SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table WHERE col1 > 1000000', ARRAY[10], 'Check count from id_taptest_table for out of band data');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_default', ARRAY[10], 'Check count from id_taptest_table_default');

SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.undo_partition_proc(''partman_test.id_taptest_table'', p_target_table := ''partman_test.id_taptest_table_target'', p_keep_table := false);".');
SELECT diag('!!! After that, run part4 of this script to check result and cleanup tests !!!');


SELECT * FROM finish();
