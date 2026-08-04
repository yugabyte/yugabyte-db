
-- ########## TIME DAILY TESTS ##########
-- Other tests: Tablespaces for table and indexes for versions PG11 and older using template table

-- Requires folder creation for tablespace and running tablespace creation before test. Cannot create tablespaces inside an explicit transaction block.
-- mkdir /opt/partman_tablespace
-- CREATE TABLESPACE mytablespace LOCATION '/opt/partman_tablespace';

-- Can remove tablespace and folder after test has completed
-- DROP TABLESPACE mytablespace;
-- rm -r /opt/partman_tablespace

-- Currently doesn't test that the child tables and indexes are in the given tablespace (pgtap doesn't have that test). Just tests the code paths. 

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(74);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text default 'stuff', col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.time_taptest_table_template (LIKE partman_test.time_taptest_table) TABLESPACE mytablespace;
ALTER TABLE partman_test.time_taptest_table_template ADD PRIMARY KEY (col1);
ALTER INDEX partman_test.time_taptest_table_template_pkey SET TABLESPACE mytablespace ;
CREATE UNIQUE INDEX ON partman_test.time_taptest_table_template (col2) TABLESPACE mytablespace;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'daily', p_template_table := 'partman_test.time_taptest_table_template');

SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman_test', 'time_taptest_table_template', 'Check template table was created');

INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(1,10), 'stuff'||generate_series(1,10), CURRENT_TIMESTAMP);

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

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table is empty. Should be impossible for native, but leaving test here just cause.');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));

INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(11,20), 'stuff'||generate_series(11,20), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(21,25), 'stuff'||generate_series(21,25), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(26,30), 'stuff'||generate_series(26,30), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(31,37), 'stuff'||generate_series(31,37), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(40,49), 'stuff'||generate_series(40,49), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(50,70), 'stuff'||generate_series(50,70), CURRENT_TIMESTAMP - '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(71,85), 'stuff'||generate_series(71,85), CURRENT_TIMESTAMP - '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(86,100), 'stuff'||generate_series(86,100), CURRENT_TIMESTAMP - '4 days'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';
-- Run to create proper future partitions
SELECT run_maintenance();
-- Insert after maintenance since native fails with no child
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(101,122), 'stuff'||generate_series(101,122), CURRENT_TIMESTAMP + '5 days'::interval);
-- Run again to create +5 partition now that data exists
SELECT run_maintenance();

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
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col2, col3) VALUES (generate_series(123,150), 'stuff'||generate_series(123,150), CURRENT_TIMESTAMP + '6 days'::interval);
-- Run again now that +6 data exists
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'13 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'13 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 day'::interval, 'YYYY_MM_DD'), 'col2', 
    'Check for col2 index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 day'::interval, 'YYYY_MM_DD'));

SELECT * FROM finish();

ROLLBACK;

