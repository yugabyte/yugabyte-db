-- ########## TIME DAILY TESTS ##########
-- Other tests: run_maintenance(p_analyze := false), check that maintenance catches up if tables are missing, full return from undo_partition()

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(211);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.time_taptest_table (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'partman', 'daily');

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
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '4 days'::interval);

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

UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table = 'partman_test.time_taptest_table';
-- Run to get +5 trigger in place
SELECT run_maintenance(p_analyze := false);
-- Insert after maintenance so new +5 day trigger is in place
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 days'::interval);
-- Run again to create proper future partitions
SELECT run_maintenance(p_analyze := false);

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

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));

GRANT DELETE ON partman_test.time_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table OWNER TO partman_owner;

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance(p_analyze := false);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 days'::interval);
SELECT run_maintenance(p_analyze := false);

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
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));

SELECT reapply_privileges('partman_test.time_taptest_table');
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

-- Test that maintenance will catch up
DO $$
BEGIN
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD');
    EXECUTE 'DROP TABLE partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD');
END
$$;

SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT run_maintenance();

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '3 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '1 day'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '2 days'::interval);

SELECT run_maintenance();

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' does not exist');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '4 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 days'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 days'::interval);

SELECT run_maintenance();

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD')||' does exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'13 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'13 days'::interval, 'YYYY_MM_DD')||' does not exist');

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 days'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT drop_partition_time('partman_test.time_taptest_table', '3 days', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');

UPDATE part_config SET retention = '2 days'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_retention_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' got moved to new schema');

SELECT results_eq('SELECT partitions_undone::int, rows_undone::int FROM undo_partition(''partman_test.time_taptest_table'', 20, p_keep_table := false)'
    , $$VALUES(15, 118)$$
    , 'Check that undo_partition() returns expected values for undoing all tables');

SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[129], 'Check count from parent table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');


SELECT * FROM finish();
ROLLBACK;

