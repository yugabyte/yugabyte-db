-- ########## TIME WEEKLY TESTS ##########
-- Other tests: Long name truncation

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(122);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table_123456789012345678901234567890123457890 TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table_123456789012345678901234567890123457890 TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table_123456789012345678901234567890123457890', 'col3', 'partman', 'weekly');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' does not exist');

SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_123456789012345678901234567890123457890'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_123456789012345678901234567890123457890', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table_123456789012345678901234567890123457890 FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 week'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 weeks'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '3 weeks'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '4 weeks'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '1 week'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '2 weeks'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '3 weeks'::interval);
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '4 weeks'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[5], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[5], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[7], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    ARRAY[10], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[21], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[15], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[15], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table_123456789012345678901234567890123457890';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 weeks'::interval);

SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' exists');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[22], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));

SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));
-- Cannot test next partitions privileges. Different lengths of months will sometimes cause an extra partition.

GRANT DELETE ON partman_test.time_taptest_table_123456789012345678901234567890123457890 TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table_123456789012345678901234567890123457890 FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table_123456789012345678901234567890123457890 OWNER TO partman_owner;

UPDATE part_config SET premake = 6 WHERE parent_table = 'partman_test.time_taptest_table_123456789012345678901234567890123457890';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 weeks'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_123456789012345678901234567890123457890', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    ARRAY[28], 'Check count from time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'));

SELECT has_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' exists');
-- Cannot test for next week not existing. Different lengths of months will sometimes cause an extra partition.
SELECT col_is_pk('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));
-- Cannot test next partitions privileges. Different lengths of months will sometimes cause an extra partition.

INSERT INTO partman_test.time_taptest_table_123456789012345678901234567890123457890 (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 weeks'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_taptest_table_123456789012345678901234567890123457890');
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));

SELECT drop_partition_time('partman_test.time_taptest_table_123456789012345678901234567890123457890', '3 weeks', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' does not exist');

UPDATE part_config SET retention = '2 weeks'::interval WHERE parent_table = 'partman_test.time_taptest_table_123456789012345678901234567890123457890';
SELECT drop_partition_time('partman_test.time_taptest_table_123456789012345678901234567890123457890', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT has_table('partman_retention_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' got moved to new schema');

SELECT undo_partition('partman_test.time_taptest_table_123456789012345678901234567890123457890', 20, p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_123456789012345678901234567890123457890', ARRAY[129], 'Check count from parent table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_12345678901234567890123456789012345_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check time_taptest_table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' does not exist');

SELECT * FROM finish();
ROLLBACK;
