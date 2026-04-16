-- ########## TIME DAILY TESTS NATIVE PROCEDURE ##########
-- Other tests: native, default out of bounds data, partition and undo with procedures instead of functions

\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(16);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table_source (
    col1 bigserial
    , col2 text 
    , col3 timestamptz DEFAULT now() NOT NULL);

INSERT INTO partman_test.time_taptest_table_source (col3) 
VALUES (generate_series(CURRENT_TIMESTAMP - '9 days'::interval, CURRENT_TIMESTAMP, '1 day'::interval));

CREATE TABLE partman_test.time_taptest_table_target (
    col1 bigserial
    , col2 text 
    , col3 timestamptz DEFAULT now() NOT NULL);


CREATE TABLE partman_test.time_taptest_table (
    col1 bigserial
    , col2 text 
    , col3 timestamptz DEFAULT now() NOT NULL
) PARTITION BY RANGE (col3);
CREATE INDEX ON partman_test.time_taptest_table(col3);

CREATE TABLE partman_test.template_time_taptest_table (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT has_table('partman_test', 'template_time_taptest_table', 'Check that template table was made');


SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'daily', p_template_table := 'partman_test.template_time_taptest_table');

SELECT has_table('partman_test', 'time_taptest_table_default', 'Check time_taptest_table_default exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

-- Check for duped indexes since it was created on both the parent and the template
SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = format('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'))::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table does not have duped index');

SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = format('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'))::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table does not have duped index');


SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = format('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'))::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table does not have duped index');


SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.partition_data_proc(''partman_test.time_taptest_table'', p_wait := 0, p_source_table := ''partman_test.time_taptest_table_source'');".');
SELECT diag('!!! After that, run part2 of this script to check result !!!');

SELECT * FROM finish();
