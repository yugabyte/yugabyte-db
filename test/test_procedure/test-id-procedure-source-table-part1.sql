-- ########## ID TESTS NATIVE - SOURCE & TARGET TABLE  ##########
-- Additional tests: turn off pg_jobmon logging, use source table to natively partition existing table, use target table to undo native partitioning


\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(12);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.id_taptest_table_source (
    col1 bigint primary key
    , col2 text 
    , col3 timestamptz DEFAULT now());

INSERT INTO partman_test.id_taptest_table_source (col1) VALUES (generate_series(1,1000000));

CREATE TABLE partman_test.id_taptest_table_target (
    col1 bigint primary key 
    , col2 text 
    , col3 timestamptz DEFAULT now());


CREATE TABLE partman_test.id_taptest_table (
    col1 bigint primary key
    , col2 text 
    , col3 timestamptz DEFAULT now()
) PARTITION BY RANGE (col1);
CREATE INDEX ON partman_test.id_taptest_table (col3);

CREATE TABLE partman_test.template_id_taptest_table (LIKE partman_test.id_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.id_taptest_table', 'col1', 'native', '100000', p_jobmon := false, p_template_table := 'partman_test.template_id_taptest_table');

SELECT has_table('partman_test', 'id_taptest_table_default', 'Check id_taptest_table_default exists');
SELECT has_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 exists');
SELECT has_table('partman_test', 'id_taptest_table_p100000', 'Check id_taptest_table_p100000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p200000', 'Check id_taptest_table_p200000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p300000', 'Check id_taptest_table_p300000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p400000', 'Check id_taptest_table_p400000 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p500000', 'Check id_taptest_table_p500000 doesn''t exists yet');

-- Check for duped indexes since it was created on both the parent and the template
SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p0'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p0 does not have duped index');

SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p100000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p100000 does not have duped index');

SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p200000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p200000 does not have duped index');

SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p300000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p300000 does not have duped index');

SELECT is_empty($$SELECT key
    FROM (SELECT indexrelid::regclass AS idx
            , (indrelid::text ||E'\n'|| indclass::text ||E'\n'|| indkey::text ||E'\n'|| coalesce(indexprs::text,'')||E'\n' || coalesce(indpred::text,'')) AS KEY FROM pg_index
                WHERE indrelid = 'partman_test.id_taptest_table_p400000'::regclass) sub 
            GROUP BY key 
            HAVING count(*)>1$$
    , 'Check that table id_taptest_table_p400000 does not have duped index');


SELECT diag('!!! In separate psql terminal, please run the following (adjusting schema if needed): "CALL partman.partition_data_proc(''partman_test.id_taptest_table'', p_wait := 0, p_source_table := ''partman_test.id_taptest_table_source'');".');
SELECT diag('!!! After that, run part2 of this script to check result !!!');

SELECT * FROM finish();

