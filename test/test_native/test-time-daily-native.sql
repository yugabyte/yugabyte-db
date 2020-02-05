-- ########## TIME DAILY TESTS ##########
-- Other tests: check that maintenance catches up if tables are missing
    -- Test using default template table. Initial child tables will have no indexes. New tables after template has indexes added should.
    -- The FK tests in this test will fail on PG11+. No way to run specific tests for specific versions easily. Just ignore for now.
    -- Test publication inheritance

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(232);

CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;
CREATE PUBLICATION partman_test_publication;

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE TABLE partman_test.time_taptest_table (col1 int, col2 text default 'stuff', col3 timestamptz NOT NULL DEFAULT now()) PARTITION BY RANGE (col3);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.time_taptest_table INCLUDING ALL);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_taptest_table TO partman_basic;
GRANT ALL ON partman_test.time_taptest_table TO partman_revoke;
ALTER TABLE partman_test.time_taptest_table OWNER TO partman_owner;

SELECT create_parent('partman_test.time_taptest_table', 'col3', 'native', 'daily', p_publications := ARRAY['partman_test_publication']);
UPDATE part_config SET inherit_privileges = TRUE;
SELECT reapply_privileges('partman_test.time_taptest_table');


SELECT is_partitioned('partman_test', 'time_taptest_table', 'Check that time_taptest_table is natively partitioned');
SELECT has_table('partman', 'template_partman_test_time_taptest_table', 'Check that default template table was created');
SELECT table_owner_is ('partman', 'template_partman_test_time_taptest_table', 'partman_owner', 
    'Check that template table ownership is set properly');

-- Add inheritable stuff to template table
ALTER TABLE partman.template_partman_test_time_taptest_table ADD PRIMARY KEY (col1);
ALTER TABLE partman.template_partman_test_time_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);

INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);

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

SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for NO primary key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'));

SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for NO foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));

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


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table is empty. Should be impossible for native, but leaving test here just cause.');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM pg_catalog.pg_publication_tables WHERE pubname = ''partman_test_publication''', ARRAY[9], 'Check that all child tables were added to publication');

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

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_taptest_table';

-- Run to create proper future partitions
SELECT run_maintenance();
-- Insert after maintenance since native fails with no child
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(101,122), CURRENT_TIMESTAMP + '5 days'::interval);
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

SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'9 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'10 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');


SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[22], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM pg_catalog.pg_publication_tables WHERE pubname = ''partman_test_publication''', ARRAY[15], 'Check that all child tables were added to publication');

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
SELECT run_maintenance();
INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(123,150), CURRENT_TIMESTAMP + '6 days'::interval);
-- Run again now that +6 data exists
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.time_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table', ARRAY[148], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    ARRAY[28], 'Check count from time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM pg_catalog.pg_publication_tables WHERE pubname = ''partman_test_publication''', ARRAY[17], 'Check that all child tables were added to publication');

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

SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'11 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');
SELECT col_is_fk('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'12 days'::interval, 'YYYY_MM_DD')||'. This test will fail in PG11+.');


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
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP+'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
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

----- This test won't work unless a default table option is added to native
--INSERT INTO partman_test.time_taptest_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 days'::interval);
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');


SELECT drop_partition_time('partman_test.time_taptest_table', '3 days', p_keep_table := false);
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');

UPDATE part_config SET retention = '2 days'::interval WHERE parent_table = 'partman_test.time_taptest_table';
SELECT drop_partition_time('partman_test.time_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_retention_test', 'time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check time_taptest_table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' got moved to new schema');

SELECT undo_partition('partman_test.time_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM  partman_test.undo_taptest', ARRAY[118], 'Check count from target table after undo');
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

SELECT hasnt_table('partman', 'template_partman_test_time_taptest_table', 'Check that template table was dropped');


SELECT * FROM finish();
ROLLBACK;

