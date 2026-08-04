
-- ########## TIME DAILY TESTS ##########
-- Other tests: Tablespaces for table and indexes on PG12+ using the real parent
    -- Generated column inheritance

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

SELECT plan(77);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.time_taptest_table (
    id int GENERATED ALWAYS AS IDENTITY
    , peaktemp_f int
    , peaktemp_c int GENERATED ALWAYS AS ((peaktemp_f - 32) * 5/9) STORED
    , logdate timestamptz NOT NULL DEFAULT now()) 
    PARTITION BY RANGE (logdate) 
    TABLESPACE mytablespace;

CREATE TABLE partman_test.time_taptest_table_template (LIKE partman_test.time_taptest_table) TABLESPACE mytablespace;
ALTER TABLE partman_test.time_taptest_table_template ADD PRIMARY KEY (id);
-- Tablespaces for unique indexes still have to be set on template table since they don't exist on real parent
ALTER INDEX partman_test.time_taptest_table_template_pkey SET TABLESPACE mytablespace ;
CREATE UNIQUE INDEX ON partman_test.time_taptest_table_template (peaktemp_f) TABLESPACE mytablespace;
-- Yes next index is sort of duped. This is just for testing proper tablespace setting
CREATE INDEX ON partman_test.time_taptest_table (id, peaktemp_f) TABLESPACE mytablespace;

SELECT create_parent('partman_test.time_taptest_table', 'logdate', 'native', 'daily', p_template_table := 'partman_test.time_taptest_table_template');

SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman_test', 'time_taptest_table_template', 'Check template table was created');

INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

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

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table is empty. Should be impossible for native, but leaving test here just cause.');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '4 days'::interval);

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
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 days'::interval);
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

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 days'::interval);
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

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), ARRAY['id'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));

SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 day'::interval, 'YYYY_MM_DD'));
SELECT is_indexed('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 day'::interval, 'YYYY_MM_DD'), 'peaktemp_f', 
    'Check for peaktemp_f index in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 day'::interval, 'YYYY_MM_DD'));

INSERT INTO partman_test.time_taptest_table (peaktemp_f, logdate) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 days'::interval);
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_default', ARRAY[11], 'Check that data outside trigger scope goes to default');


-- Check that all generated columns had the proper values set
-- SQL to generate csv list of id values:
--- with i as (select id from partman_test.time_taptest_table order by id asc) select string_agg(id::text, ',') from i;
SELECT results_eq('SELECT id FROM partman_test.time_taptest_table ORDER BY id ASC', ARRAY[1,2,3,4,5,6,7,8,9,10,12,13,14,15,16,17,18,19,20,21,23,24,25,26,27,29,30,31,32,33,35,36,37,38,39,40,41,43,44,45,46,47,48,49,50,51,52,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,160,161,162,163,164,165,166,167,168,169,170], 'Check all identity generated column values');
-- SQL to generate csv list of peaktemp_c values:
--- with c as (select peaktemp_c from partman_test.time_taptest_table order by id asc) select string_agg(peaktemp_c, ',') from c;
SELECT results_eq('SELECT peaktemp_c FROM partman_test.time_taptest_table ORDER BY id ASC', ARRAY[-17,-16,-16,-15,-15,-14,-13,-13,-12,-12,-11,-11,-10,-10,-9,-8,-8,-7,-7,-6,-6,-5,-5,-4,-3,-3,-2,-2,-1,-1,0,0,0,1,1,2,2,4,5,5,6,6,7,7,8,8,9,10,10,11,11,12,12,13,13,14,15,15,16,16,17,17,18,18,19,20,20,21,21,22,22,23,23,24,25,25,26,26,27,27,28,28,29,30,30,31,31,32,32,33,33,34,35,35,36,36,37,37,38,38,39,40,40,41,41,42,42,43,43,44,45,45,46,46,47,47,48,48,49,50,50,51,51,52,52,53,53,54,55,55,56,56,57,57,58,58,59,60,60,61,61,62,62,63,63,64,65,65,93,93,94,95,95,96,96,97,97,98,98], 'Check all user generated column values');

SELECT * FROM finish();

ROLLBACK;

