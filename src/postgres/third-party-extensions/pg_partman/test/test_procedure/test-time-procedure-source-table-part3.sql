\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(9);

-- child table should now be available for data
INSERT INTO partman_test.time_taptest_table (col3) VALUES (CURRENT_TIMESTAMP + '5 days'::interval);

-- Data exists for +5 days, with 5 premake so +10 day table should exist
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' exists');

-- Check for duped indexes since it was created on both the parent and the template
SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = format('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'))::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table does not have duped index');

SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_default', 'Check that default table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.undo_partition_proc(''partman_test.time_taptest_table'', p_keep_table := false, p_wait := 0, p_target_table := ''partman_test.time_taptest_table_target'');".');
SELECT diag('!!! After that, run part4 of this script to check result and cleanup test objects !!!');


SELECT * FROM finish();
