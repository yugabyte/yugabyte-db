\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(11);

SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_source LIMIT 1', 'Check that source table is empty');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[1000000], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[99999], 'Check count from id_taptest_table_p0');

SELECT has_table('partman_test', 'id_taptest_table_p500000', 'Check id_taptest_table_p500000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p600000', 'Check id_taptest_table_p600000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p700000', 'Check id_taptest_table_p700000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p800000', 'Check id_taptest_table_p800000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p900000', 'Check id_taptest_table_p900000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p1000000', 'Check id_taptest_table_p1000000 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p1100000', 'Check id_taptest_table_p1100000 doesn''t exists yet');

-- Check for duped indexes since it was created on both the parent and the template
SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p500000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p500000 does not have duped index');


SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.run_maintenance_proc();".');
SELECT diag('!!! After that, run part3 of this script to check result !!!');


SELECT * FROM finish();
