\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(11);

-- YB: reduced number of rows by factor of 10 to prevent test timeout
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_source LIMIT 1', 'Check that source table is empty');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[100000], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9999], 'Check count from id_taptest_table_p0');

SELECT has_table('partman_test', 'id_taptest_table_p50000', 'Check id_taptest_table_p50000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p60000', 'Check id_taptest_table_p60000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p70000', 'Check id_taptest_table_p70000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p80000', 'Check id_taptest_table_p80000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p90000', 'Check id_taptest_table_p90000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p100000', 'Check id_taptest_table_p100000 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p110000', 'Check id_taptest_table_p110000 doesn''t exists yet');

-- Check for duped indexes since it was created on both the parent and the template
SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p50000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p50000 does not have duped index');


SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.run_maintenance_proc();".');
SELECT diag('!!! After that, run part3 of this script to check result !!!');


SELECT * FROM finish();
