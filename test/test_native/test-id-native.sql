-- ########## ID DYNAMIC TESTS ##########
-- Additional tests: turn off pg_jobmon logging, UNLOGGED, PUBLIC role, start with higher number, inherit FK, inherit privileges
    -- Test using a pre-created template table and passing to create_parent. Should allow indexes to be made for initial children.
    -- Tests that foreign keys and normal indexes for PG10 use the template and for PG11 they use the parent. Also since this is id partitioning, we can use the partition key for primary key, so that should work from parent on PG11 as well.

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(120);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE UNLOGGED TABLE partman_test.id_taptest_table (
    col1 bigint 
    , col2 text not null default 'stuff'
    , col3 timestamptz DEFAULT now()
    , col4 text) PARTITION BY RANGE (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.id_taptest_table INCLUDING ALL);
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table TO partman_basic, PUBLIC;
GRANT ALL ON partman_test.id_taptest_table TO partman_revoke;
-- Template table
CREATE UNLOGGED TABLE partman_test.template_id_taptest_table (LIKE partman_test.id_taptest_table);

DO $pg11_objects_check$
BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
    -- Create on parent table
    ALTER TABLE partman_test.id_taptest_table ADD PRIMARY KEY (col1);
    ALTER TABLE partman_test.id_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);
    CREATE INDEX ON partman_test.id_taptest_table (col3);
ELSE
    -- Create on template table
    ALTER TABLE partman_test.template_id_taptest_table ADD PRIMARY KEY (col1);
    ALTER TABLE partman_test.template_id_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);
END IF;
END $pg11_objects_check$;

-- Always create the index on teh template also so that we can test excluding duplicates.
CREATE INDEX ON partman_test.template_id_taptest_table (col3);

-- Regular unique indexes do not work on native in PG11 if the partition key isn't included
CREATE UNIQUE INDEX ON partman_test.template_id_taptest_table (col4);

SELECT create_parent('partman_test.id_taptest_table', 'col1', 'native', '10', p_jobmon := false, p_start_partition := '3000000000', p_template_table := 'partman_test.template_id_taptest_table');
UPDATE part_config SET inherit_privileges = TRUE;
SELECT reapply_privileges('partman_test.id_taptest_table');

INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000001,3000000009), 'stuff'||generate_series(3000000001,3000000009));

SELECT has_table('partman_test', 'id_taptest_table_p3000000000', 'Check id_taptest_table_p3000000000 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000010', 'Check id_taptest_table_p3000000010 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000020', 'Check id_taptest_table_p3000000020 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000030', 'Check id_taptest_table_p3000000030 exists');
SELECT has_table('partman_test', 'id_taptest_table_p3000000040', 'Check id_taptest_table_p3000000040 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000000', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000000');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000010', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000010');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000020', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000020');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000030', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000030');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000040', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000040');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000000', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000000');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000010', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000010');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000020', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000020');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000030', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000030');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000040', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000040');
SELECT is_indexed('partman_test', 'id_taptest_table_p3000000000', 'col4', 'Check that unique index was inherited to id_taptest_table_p3000000000');
SELECT is_indexed('partman_test', 'id_taptest_table_p3000000010', 'col4', 'Check that unique index was inherited to id_taptest_table_p3000000010');
SELECT is_indexed('partman_test', 'id_taptest_table_p3000000020', 'col4', 'Check that unique index was inherited to id_taptest_table_p3000000020');
SELECT is_indexed('partman_test', 'id_taptest_table_p3000000030', 'col4', 'Check that unique index was inherited to id_taptest_table_p3000000030');
SELECT is_indexed('partman_test', 'id_taptest_table_p3000000040', 'col4', 'Check that unique index was inherited to id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p3000000040');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table''::regclass', ARRAY['u'], 'Check that parent table is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000000''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000000 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000010''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000010 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000020''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000020 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000030''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000030 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000040''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000040 is unlogged');

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000000', ARRAY[9], 'Check count from id_taptest_table_p3000000000');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_taptest_table FROM partman_revoke, PUBLIC;

SELECT run_maintenance();
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000010,3000000025), 'stuff'||generate_series(3000000010,3000000025));
-- Run again to make new partition based on latest data
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000010', ARRAY[10], 'Check count from id_taptest_table_p3000000010');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[6], 'Check count from id_taptest_table_p3000000020');

SELECT has_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000050''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000050 is unlogged');
SELECT has_table('partman_test', 'id_taptest_table_p3000000060', 'Check id_taptest_table_p3000000060 exists yet');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000060''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000060 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 doesn''t exists yet');
--SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000050', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000050');
--SELECT col_isnt_fk('partman_test', 'id_taptest_table_p3000000050', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000060');

GRANT DELETE ON partman_test.id_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.id_taptest_table FROM partman_revoke, PUBLIC;
ALTER TABLE partman_test.id_taptest_table OWNER TO partman_owner;
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000026,3000000038), 'stuff'||generate_series(3000000026,3000000038));

SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[10], 'Check count from id_taptest_table_p3000000020');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000030', ARRAY[9], 'Check count from id_taptest_table_p3000000030');

SELECT has_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000070''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000070 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000080', 'Check id_taptest_table_p3000000080 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000060', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000060');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000070', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000070');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000060', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000060');
SELECT col_is_fk('partman_test', 'id_taptest_table_p3000000070', 'col2', 'Check that foreign key was inherited to id_taptest_table_p3000000070');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000070', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000070');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000070', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000070');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000070', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000070');

-- Requires default table option
--INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(3000000200,3000000210), 'stuff'||generate_series(3000000200,3000000210));
--SELECT run_maintenance();
--SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.id_taptest_table');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000070', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p3000000070');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000070', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p3000000070');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000000', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000000');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000010', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000010');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000020', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000020');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000030', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000030');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000040', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000040');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000050', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000050');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000060', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000060');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p3000000070', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p3000000070');

-- Max value is 38 above
SELECT drop_partition_id('partman_test.id_taptest_table', '20', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000000', 'Check id_taptest_table_p3000000000 doesn''t exists anymore');

UPDATE part_config SET retention = '10' WHERE parent_table = 'partman_test.id_taptest_table';
SELECT drop_partition_id('partman_test.id_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000010', 'Check id_taptest_table_p3000000010 doesn''t exists anymore');
SELECT has_table('partman_retention_test', 'id_taptest_table_p3000000010', 'Check id_taptest_table_p3000000010 got moved to new schema');

SELECT undo_partition('partman_test.id_taptest_table', p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000020', 'Check id_taptest_table_p3000000020 does not exist');

-- Test keeping the rest of the tables
SELECT undo_partition('partman_test.id_taptest_table', 10, p_target_table := 'partman_test.undo_taptest');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.undo_taptest', ARRAY[19], 'Check count from parent table after undo');
SELECT has_table('partman_test', 'id_taptest_table_p3000000030', 'Check id_taptest_table_p3000000030 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p3000000030', 'Check child table had its data removed id_taptest_table_p3000000030');
SELECT has_table('partman_test', 'id_taptest_table_p3000000040', 'Check id_taptest_table_p3000000040 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p3000000040', 'Check child table had its data removed id_taptest_table_p3000000040');
SELECT has_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p3000000050', 'Check child table had its data removed id_taptest_table_p3000000050');
SELECT has_table('partman_test', 'id_taptest_table_p3000000060', 'Check id_taptest_table_p3000000060 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p3000000060', 'Check child table had its data removed id_taptest_table_p3000000060');
SELECT has_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p3000000070', 'Check child table had its data removed id_taptest_table_p3000000070');

SELECT hasnt_table('partman_test', 'template_id_taptest_table', 'Check that template table was dropped');

SELECT * FROM finish();
ROLLBACK;
