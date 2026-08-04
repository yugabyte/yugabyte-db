\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(19);

SELECT is_empty('SELECT * FROM partman_test.id_taptest_table LIMIT 1', 'Check that main partition parent set is empty');

SELECT is_empty('SELECT * FROM part_config WHERE parent_table = ''partman_test.id_taptest_table'' ', 'Check config has been cleared');

SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_target', ARRAY[1000010], 'Check count from target table');

SELECT hasnt_table('partman_test', 'id_taptest_table_default', 'Check id_taptest_table_default was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p100000', 'Check id_taptest_table_p100000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p200000', 'Check id_taptest_table_p200000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p300000', 'Check id_taptest_table_p300000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p400000', 'Check id_taptest_table_p400000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p500000', 'Check id_taptest_table_p500000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p600000', 'Check id_taptest_table_p600000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p700000', 'Check id_taptest_table_p700000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p800000', 'Check id_taptest_table_p800000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p900000', 'Check id_taptest_table_p900000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1000000', 'Check id_taptest_table_p1000000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1100000', 'Check id_taptest_table_p1100000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1200000', 'Check id_taptest_table_p1200000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1300000', 'Check id_taptest_table_p1300000 was removed');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1400000', 'Check id_taptest_table_p1400000 was removed');

DROP SCHEMA IF EXISTS partman_test CASCADE;

SELECT diag('!!! Final test complete !!!');

SELECT * FROM finish();
