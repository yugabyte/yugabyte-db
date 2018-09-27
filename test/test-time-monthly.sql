-- ########## TIME MONTHLY TESTS ##########
-- Other tests: Make sure option to not inherit foreign keys works

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(166);
CREATE SCHEMA partman_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.fk_test_reference (col2 text not null);
CREATE UNIQUE INDEX ON partman_test.fk_test_reference(col2);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE TABLE partman_test.time_taptest_table (
    col1 int primary key
    , col2 text not null default 'stuff'
    , col3 timestamptz NOT NULL DEFAULT now()
    , FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2) MATCH SIMPLE NOT DEFERRABLE);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'partman', 'monthly', p_inherit_fk := false);
--SELECT create_parent('partman_test.time_taptest_table', 'col3', 'time', 'monthly');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'5 months'::interval, 'YYYY_MM')||' does not exist');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 month'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 months'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '3 months'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '4 months'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '1 month'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '2 months'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '3 months'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '4 months'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));

UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 months'::interval);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM')||' does not exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'5 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'6 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'7 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'8 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'9 months'::interval, 'YYYY_MM'));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));

GRANT DELETE ON partman_test.time_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table OWNER TO partman_owner;

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 months'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));

-- +5 month data was inserted above so 2 children get made
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'12 months'::interval, 'YYYY_MM')||' does not exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'col2', 
    'Check that foreign key was NOT inherited for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 months'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_taptest_table');
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'));

-- Currently unable to do drop_partition test reliably for monthly due to differing month lengths (sometimes drops 2 partitions instead of 1)
SELECT undo_partition('partman_test.time_taptest_table', 20, p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[159], 'Check count from parent table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 month'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 month'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'6 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'7 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'8 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'9 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'10 months'::interval, 'YYYY_MM')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'11 months'::interval, 'YYYY_MM')||' does not exist');

SELECT * FROM finish();
ROLLBACK;
