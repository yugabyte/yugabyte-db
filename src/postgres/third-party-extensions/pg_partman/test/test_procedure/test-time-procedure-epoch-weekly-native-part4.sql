\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(15);

SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_target', ARRAY[141], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' does not exist (now)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' does not exist (+1 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' does not exist (+2 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' does not exist (+3 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' does not exist (+4 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' does not exist (-1 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' does not exist (-2 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' does not exist (-3 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' does not exist (-4 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' does not exist (-5 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' does not exist (-6 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW')||' does not exist (-7 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'8 weeks'::interval, 'IYYY"w"IW')||' does not exist (-8 weeks)');

SELECT hasnt_table('partman_test', 'time_taptest_table_default', 'Check time_taptest_table_default does not exist');


DROP SCHEMA IF EXISTS partman_test CASCADE;

SELECT diag('!!! Final test complete !!!');

SELECT * FROM finish();

