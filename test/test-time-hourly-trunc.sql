-- ########## TIME TESTS ##########
-- Other tests: Long name truncation

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(122);
CREATE SCHEMA partman_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.time_taptest_table_1234567890123456789012345678901234567890 (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
INSERT INTO partman_test.time_taptest_table_1234567890123456789012345678901234567890 (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table_1234567890123456789012345678901234567890 TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table_1234567890123456789012345678901234567890 TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table_1234567890123456789012345678901234567890', 'col3', 'partman', 'hourly');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));

SELECT results_eq('SELECT partition_data_time(''partman_test.time_taptest_table_1234567890123456789012345678901234567890'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_1234567890123456789012345678901234567890', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_1234567890123456789012345678901234567890', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 
    ARRAY[10], 'Check count from time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table_1234567890123456789012345678901234567890 FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table_1234567890123456789012345678901234567890 (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 hour'::interval);
INSERT INTO partman_test.time_taptest_table_1234567890123456789012345678901234567890 (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 hours'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table_1234567890123456789012345678901234567890', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_1234567890123456789012345678901234567890', ARRAY[25], 'Check count from time_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    ARRAY[10], 'Check count from time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    ARRAY[5], 'Check count from time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));

UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table = 'partman_test.time_taptest_table_1234567890123456789012345678901234567890';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));

GRANT DELETE ON partman_test.time_taptest_table_1234567890123456789012345678901234567890 TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table_1234567890123456789012345678901234567890 FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table_1234567890123456789012345678901234567890 OWNER TO partman_owner;

UPDATE part_config SET premake = 6, optimize_trigger = 6 WHERE parent_table = 'partman_test.time_taptest_table_1234567890123456789012345678901234567890';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'9 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'9 hours'::interval, 'YYYY_MM_DD_HH24MI')||' exists');
SELECT col_is_pk('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));

INSERT INTO partman_test.time_taptest_table_1234567890123456789012345678901234567890 (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 hours'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table_1234567890123456789012345678901234567890', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_taptest_table_1234567890123456789012345678901234567890');
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));

SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_privs_are('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP), 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'));

SELECT undo_partition('partman_test.time_taptest_table_1234567890123456789012345678901234567890', 20);
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)-'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'1 hour'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'2 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'3 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'4 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'5 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'6 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'7 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');
SELECT has_table('partman_test', 'time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI'), 
    'Check time_taptest_table_123456789012345678901234567_p'||to_char(date_trunc('hour', CURRENT_TIMESTAMP)+'8 hours'::interval, 'YYYY_MM_DD_HH24MI')||' is empty');

SELECT * FROM finish();
ROLLBACK;
