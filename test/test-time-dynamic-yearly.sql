-- ########## TIME DYNAMIC TESTS ##########
-- Other tests: Multi-column foreign key

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
CREATE SCHEMA partman_test;

SELECT plan(90);
SELECT set_config('search_path','partman, public',false);
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.fk_test_reference (col2 text not null, col4 text not null);
CREATE UNIQUE INDEX ON partman_test.fk_test_reference(col2, col4);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff', 'stuff');

CREATE TABLE partman_test.time_dynamic_table (
    col1 int primary key
    , col2 text not null default 'stuff'
    , col3 timestamptz NOT NULL DEFAULT now()
    , col4 text not null default 'stuff'
    , FOREIGN KEY (col2, col4) REFERENCES partman_test.fk_test_reference(col2, col4));
INSERT INTO partman_test.time_dynamic_table (col1, col3) VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON partman_test.time_dynamic_table TO partman_basic;
GRANT ALL ON partman_test.time_dynamic_table TO partman_revoke;

SELECT create_parent('partman_test.time_dynamic_table', 'col3', 'time-dynamic', 'yearly');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||' exists');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY')||' exists');
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));

SELECT results_eq('SELECT partition_data_time(''partman_test.time_dynamic_table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.time_dynamic_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_dynamic_table', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 
    ARRAY[10], 'Check count from time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.time_dynamic_table FROM partman_revoke;
INSERT INTO partman_test.time_dynamic_table (col1, col3) VALUES (generate_series(11,20), CURRENT_TIMESTAMP + '1 year'::interval);
INSERT INTO partman_test.time_dynamic_table (col1, col3) VALUES (generate_series(21,25), CURRENT_TIMESTAMP + '2 years'::interval);

SELECT is_empty('SELECT * FROM ONLY partman_test.time_dynamic_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_dynamic_table', ARRAY[25], 'Check count from time_dynamic_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 
    ARRAY[10], 'Check count from time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 
    ARRAY[5], 'Check count from time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));

UPDATE part_config SET premake = 5 WHERE parent_table = 'partman_test.time_dynamic_table';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY')||' exists');
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_basic', 
ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));

GRANT DELETE ON partman_test.time_dynamic_table TO partman_basic;
REVOKE ALL ON partman_test.time_dynamic_table FROM partman_revoke;
ALTER TABLE partman_test.time_dynamic_table OWNER TO partman_owner;

UPDATE part_config SET premake = 6 WHERE parent_table = 'partman_test.time_dynamic_table';
SELECT run_maintenance();
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY')||' exists');
SELECT hasnt_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'7 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'7 years'::interval, 'YYYY')||' exists');
SELECT col_is_pk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), ARRAY['col1'], 
    'Check for primary key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));
SELECT col_is_fk('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), ARRAY['col2', 'col4'], 
    'Check for inherited foreign key in time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 'partman_revoke', 
    ARRAY['SELECT'], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));

INSERT INTO partman_test.time_dynamic_table (col1, col3) VALUES (generate_series(200,210), CURRENT_TIMESTAMP + '20 years'::interval);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.time_dynamic_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.time_dynamic_table');
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_basic', 
    ARRAY['SELECT','INSERT','UPDATE', 'DELETE'], 
    'Check partman_basic privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));

SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));
SELECT table_privs_are('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_revoke', 
    '{}'::text[], 'Check partman_revoke privileges of time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));

SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'));
SELECT table_owner_is ('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 'partman_owner', 
    'Check that ownership change worked for time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'));

SELECT undo_partition_time('partman_test.time_dynamic_table', 20);
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'1 year'::interval, 'YYYY')||' is empty');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'2 years'::interval, 'YYYY')||' is empty');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'3 years'::interval, 'YYYY')||' is empty');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'4 years'::interval, 'YYYY')||' is empty');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'5 years'::interval, 'YYYY')||' is empty');
SELECT has_table('partman_test', 'time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY')||' still exists');
SELECT is_empty('SELECT * FROM partman_test.time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY'), 
    'Check time_dynamic_table_p'||to_char(CURRENT_TIMESTAMP+'6 years'::interval, 'YYYY')||' is empty');

SELECT * FROM finish();
ROLLBACK;
