-- ########## TIME 9 WEEKS TESTS ##########

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(79);

CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text default 'stuff', col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', '11 weeks', p_date_trunc_interval := 'week');

SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman', 'template_partman_test_time_taptest_table', 'Check that default template table was created');

-- Add inheritable stuff to template table
ALTER TABLE partman.template_partman_test_time_taptest_table ADD PRIMARY KEY (col1);

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'), 'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD')||' exists (+11 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD')||' exists (+22 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD')||' exists (+33 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD')||' exists (+44 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+55 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD')||' exists (-11 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD')||' exists (-22 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD')||' exists (-33 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD')||' exists (-44 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'55 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'55 weeks'::interval, 'YYYY_MM_DD')||' does not exist (-55 weeks)');

SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'4 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'4 days'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table is empty. Should be impossible for native, but leaving test here just cause.');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '11 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '22 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '33 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '44 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '11 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '22 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '33 weeks'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '44 weeks'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';

-- Run to create proper future partitions
SELECT run_maintenance();
-- Insert after maintenance since native fails with no child
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '55 weeks'::interval);
-- Run again to create +5 partition now that data exists
SELECT run_maintenance();

-- Data exists for +55 weeks, with 5 premake so +110 week table should exist
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD')||' exists (+55 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD')||' exists (+66 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD')||' exists (+77 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD')||' exists (+88 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD')||' exists (+99 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD')||' exists (+110 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+121 weeks)');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD')||' (+55 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD')||' (+66 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD')||' (+77 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD')||' (+88 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD')||' (+99 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD')||' (+110 weeks)');

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD')||' (+55 weeks)');

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '66 weeks'::interval);
-- Run again now that +6 data exists
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD')||' (+66 weeks)');

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD')||' exists (+121 weeks)');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD')||' exists (+132 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'143 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'143 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+143 weeks)');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD')||' (+121 weeks)');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD')||' (+132 weeks)');


INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '200 weeks'::interval);
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[159], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside existing child tables goes to default');


SELECT drop_partition_time('partman_test.time_taptest_table', '33 weeks', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'44 weeks'::interval, 'YYYY_MM_DD')||' does not exist (-44 weeks)');

UPDATE part_config SET retention = '22 weeks'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'33 weeks'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM  partman_test.undo_taptest', ARRAY[129], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP), 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'11 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+11 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'22 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+22 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'33 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+33 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'44 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+44 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'55 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+55 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'66 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+66 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'77 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+77 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'88 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+88 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'99 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+99 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'110 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+110 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'121 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+121 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)+'132 weeks'::interval, 'YYYY_MM_DD')||' does not exist (+132 weeks)');

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'11 weeks'::interval, 'YYYY_MM_DD')||' does not exist (-11 weeks)');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(date_trunc('week',CURRENT_TIMESTAMP)-'22 weeks'::interval, 'YYYY_MM_DD')||' does not exist (-22 weeks)');

SELECT hasnt_table('partman', 'template_partman_test_time_taptest_table', 'Check that template table was dropped');

SELECT * FROM finish();
ROLLBACK;

