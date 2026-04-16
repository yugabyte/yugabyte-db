-- ########## TIME 9 WEEKS TESTS ##########

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(49);

CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text default 'stuff', col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', '1 day');

SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman', 'template_partman_test_time_taptest_table', 'Check that default template table was created');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists (+1 day)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+4 days'::interval, 'YYYY_MM_DD')||' exists (+4 days)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'-4 days'::interval, 'YYYY_MM_DD')||' exists (-4 days)');

-- Create subpartition 
-- Need to set a start partition to ensure that the hourly blocks start at midnight. Otherwise, they will be created based on "now", which means the child tables could miss coverage of some hours during the day.
SELECT partman.create_sub_parent('partman_test.time_taptest_table', 'col3', 'native', '8 hours', p_native_check := 'yes', p_date_trunc_interval := 'day', p_start_partition := date_trunc('day', CURRENT_TIMESTAMP)::text);
--Insert "now" data here due to child table destruction
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) + '24 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) + '32 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) + '40 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) + '48 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) + '56 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) + '64 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) + '72 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) + '80 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) + '88 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), date_trunc('day',CURRENT_TIMESTAMP) + '96 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), date_trunc('day',CURRENT_TIMESTAMP) + '104 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), date_trunc('day',CURRENT_TIMESTAMP) + '112 hours'::interval);

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) - '24 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) - '32 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), date_trunc('day',CURRENT_TIMESTAMP) - '40 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) - '48 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) - '56 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), date_trunc('day',CURRENT_TIMESTAMP) - '64 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) - '72 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) - '80 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), date_trunc('day',CURRENT_TIMESTAMP) - '88 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), date_trunc('day',CURRENT_TIMESTAMP) - '96 hours'::interval);

SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'24 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'48 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'72 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'96 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'24 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'48 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'72 hours'::interval, 'YYYY_MM_DD'), 20);
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'96 hours'::interval, 'YYYY_MM_DD'), 20);

SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_default', 'Check that default table has had no data inserted to it');

-- Start +4 days validation

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'24 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'24 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(CURRENT_TIMESTAMP+'24 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'24 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+24hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'32 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'32 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'32 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'32 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+32hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'40 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'40 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'40 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'40 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+40hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'48 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'48 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'48 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'48 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+48hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'56 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+56hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'64 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'64 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'64 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'64 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+64hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'72 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'72 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'72 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'72 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+72hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'80 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'80 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'80 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'80 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+80hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'88 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'88 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'88 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'88 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+88hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'96 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'96 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'96 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'96 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+96hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'104 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'104 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'104 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'104 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+104hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'112 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'112 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'112 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'112 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+112hrs)');

-- End + 4 days


UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';

-- Run to create proper future partitions
SELECT run_maintenance();

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+5 days'::interval, 'YYYY_MM_DD')||' exists (+5 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+120hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+128hrs)');


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+6 days'::interval, 'YYYY_MM_DD')||' exists (+6 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+144hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'152 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'144 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'152 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+152hrs)');


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+7 days'::interval, 'YYYY_MM_DD')||' exists (+7 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+168hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'176 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'168 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'176 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+176hrs)');


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+8 days'::interval, 'YYYY_MM_DD')||' exists (+8 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+192hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'200 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'192 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'200 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+200hrs)');


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+9 days'::interval, 'YYYY_MM_DD')||' exists (+9 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+216hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'224 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'216 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'224 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+224hrs)');


SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+10 days'::interval, 'YYYY_MM_DD')||' does not exist (+10 days)');


-- Insert after maintenance since native would put it into default 
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), date_trunc('day',CURRENT_TIMESTAMP) + '120 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), date_trunc('day',CURRENT_TIMESTAMP) + '128 hours'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), date_trunc('day',CURRENT_TIMESTAMP) + '136 hours'::interval);

-- Partition the data out since new child tables cannot be created with data in the +120 hour default
SELECT partman.partition_data_time('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'120 hours'::interval, 'YYYY_MM_DD'), 20);

-- Run again to create expected +5 premake partitions now that data exists
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_default', 'Check that default table has had no data inserted to it');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD'), 
    ARRAY[30], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||' (+5 days)');


SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+120hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'128 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+128hrs)');

SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'136 hours'::interval, 'YYYY_MM_DD_HH24MISS'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'120 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'136 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' (+136hrs)');


SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+10 days'::interval, 'YYYY_MM_DD')||' exists (+10 days)');

SELECT has_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' exists (+240hrs)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'248 hours'::interval, 'YYYY_MM_DD_HH24MISS')
    , 'Check time_taptest_table_p'||
        to_char(date_trunc('day',CURRENT_TIMESTAMP)+'240 hours'::interval, 'YYYY_MM_DD')||
        '_p'||to_char(date_trunc('day',CURRENT_TIMESTAMP)+'248 hours'::interval, 'YYYY_MM_DD_HH24MISS')||' does not exist (+248hrs)');


SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'+11 days'::interval, 'YYYY_MM_DD')||' does not exist (+11 days)');


SELECT drop_partition_time('partman_test.time_taptest_table', '3 days', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist (4 days)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist (5 days)');

UPDATE part_config SET retention = '1 day', retention_keep_table = 'false' WHERE parent_table = 'partman_test.time_taptest_table';
SELECT partman.run_maintenance();
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist (2 days)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist (3 days)');


SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT undo_partition('partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);


SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);

SELECT is_empty('select relname from pg_class c join pg_namespace n on c.relnamespace = n.oid where n.nspname = ''partman_test'' and c.relname ~ ''time_taptest_table_p''', 'Check that no partition child tables for this set exist in system catalogs');
SELECT * FROM finish();

ROLLBACK;

