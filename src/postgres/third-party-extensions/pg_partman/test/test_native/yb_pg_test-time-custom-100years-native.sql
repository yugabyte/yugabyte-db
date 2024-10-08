-- ########## TIME CUSTOM TESTS NATIVE ##########
-- Other tests: Use undo_partition_native(), inherit privileges

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

-- BEGIN; YB: Transactional DDL not supported
SELECT set_config('search_path','partman, public',false);

SELECT plan(132); -- YB: decreased number of tests
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text, col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.time_taptest_table_template (LIKE partman_test.time_taptest_table INCLUDING ALL);
ALTER TABLE partman_test.time_taptest_table_template ADD PRIMARY KEY (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);

DO $pg11_objects_check$
BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
    -- Create on parent table
    CREATE INDEX ON partman_test.time_taptest_table (col3);
ELSE
    -- Create on template table
    CREATE INDEX ON partman_test.time_taptest_table_template (col3);
END IF;
END $pg11_objects_check$;


GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table TO partman_revoke;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', '100 years', p_template_table := 'partman_test.time_taptest_table_template');
UPDATE part_config SET inherit_privileges = TRUE;
SELECT reapply_privileges('partman_test.time_taptest_table');


INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY')||' does not exist');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'500 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'500 years'::interval, 'YYYY')||' does not exist');

SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_taptest_table FROM partman_revoke;
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '100 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '200 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(26,30), CURRENT_TIMESTAMP + '300 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(31,37), CURRENT_TIMESTAMP + '400 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(40,49), CURRENT_TIMESTAMP - '100 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(50,70), CURRENT_TIMESTAMP - '200 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(71,85), CURRENT_TIMESTAMP - '300 years'::interval);
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(86,100), CURRENT_TIMESTAMP - '400 years'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 
    ARRAY[5], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 
    ARRAY[7], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 
    ARRAY[21], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 
    ARRAY[15], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '500 years'::interval);

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY')||' does not exist');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));

-- TODO Change this to check the default partition when that is added
-- SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));

GRANT DELETE ON partman_test.time_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.time_taptest_table FROM partman_revoke;
ALTER TABLE partman_test.time_taptest_table OWNER TO partman_owner;

UPDATE part_config SET premake = 6 WHERE parent_table = 'partman_test.time_taptest_table';
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '600 years'::interval);

-- TODO Change this to check the default partition when that is added
--SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));

SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1200 years'::interval, 'YYYY'), 
    'Check time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1200 years'::interval, 'YYYY')||' does not exist');
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE','DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE','DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));

-- TODO Change this to check the default partition when that is added
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '2000 years'::interval);
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_taptest_table');
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));

SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'));

SELECT drop_partition_time('partman_test.time_taptest_table', '300 years', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'400 years'::interval, 'YYYY')||' does not exist');

UPDATE part_config SET retention = '200 years'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY')||' does not exist');
SELECT has_table('partman_retention_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'300 years'::interval, 'YYYY')||' got moved to new schema');

/* YB: undo_partition not supported
SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM partman_test.undo_taptest', ARRAY[118], 'Check count from target table after undo');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP), 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'100 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)-'200 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'100 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'200 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'300 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'400 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'500 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'600 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'700 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'800 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'900 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1000 years'::interval, 'YYYY')||' does not exist');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY'), 
    'Check time_taptest_table_'||to_char(date_trunc('century', CURRENT_TIMESTAMP)+'1100 years'::interval, 'YYYY')||' does not exist');


SELECT hasnt_table('partman_test', 'time_taptest_table_template', 'Check that template table was dropped');
*/ -- YB

SELECT * FROM finish();
-- ROLLBACK; YB: Transactional DDL not supported
