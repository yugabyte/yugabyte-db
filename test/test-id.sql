-- ########## ID DYNAMIC TESTS ##########
-- Additional tests: turn off pg_jobmon logging, UNLOGGED, Make sure option to not inherit foreign keys works, larger than necessary p_batch_count to partition_data_id(), retention

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(118);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE UNLOGGED TABLE partman_test.id_taptest_table (
    col1 int primary key
    , col2 text not null default 'stuff' references partman_test.fk_test_reference (col2)
    , col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table TO partman_basic;
GRANT ALL ON partman_test.id_taptest_table TO partman_revoke;

SELECT results_eq('SELECT create_parent(''partman_test.id_taptest_table'', ''col1'', ''id'', ''10'', p_inherit_fk := false, p_jobmon := false)::text', ARRAY['true'], 'Check that create_parent() returns true');
SELECT has_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 exists');
SELECT has_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 exists');
SELECT has_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 exists');
SELECT has_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 exists');
SELECT has_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0', ARRAY['col1'], 'Check for primary key in id_taptest_table_p0');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10', ARRAY['col1'], 'Check for primary key in id_taptest_table_p10');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20', ARRAY['col1'], 'Check for primary key in id_taptest_table_p20');
SELECT col_is_pk('partman_test', 'id_taptest_table_p30', ARRAY['col1'], 'Check for primary key in id_taptest_table_p30');
SELECT col_is_pk('partman_test', 'id_taptest_table_p40', ARRAY['col1'], 'Check for primary key in id_taptest_table_p40');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p0', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p0');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p10', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p10');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p20', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p20');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p30', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p30');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p40', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_p40');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table''::regclass', ARRAY['u'], 'Check that parent table is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p0''::regclass', ARRAY['u'], 'Check that id_taptest_table_p0 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p10''::regclass', ARRAY['u'], 'Check that id_taptest_table_p10 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p20''::regclass', ARRAY['u'], 'Check that id_taptest_table_p20 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p30''::regclass', ARRAY['u'], 'Check that id_taptest_table_p30 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p40''::regclass', ARRAY['u'], 'Check that id_taptest_table_p40 is unlogged');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table'', p_batch_count := 5)::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from id_taptest_table_p0');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_taptest_table FROM partman_revoke;
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(10,25));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[6], 'Check count from id_taptest_table_p20');

SELECT has_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p50''::regclass', ARRAY['u'], 'Check that id_taptest_table_p50 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p50', ARRAY['col1'], 'Check for primary key in id_taptest_table_p50');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p50', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p50');

GRANT DELETE ON partman_test.id_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.id_taptest_table FROM partman_revoke;
ALTER TABLE partman_test.id_taptest_table OWNER TO partman_owner;
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(26,38));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[10], 'Check count from id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p30', ARRAY[9], 'Check count from id_taptest_table_p30');

SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p60''::regclass', ARRAY['u'], 'Check that id_taptest_table_p60 is unlogged');
SELECT has_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p70''::regclass', ARRAY['u'], 'Check that id_taptest_table_p70 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p80', 'Check id_taptest_table_p80 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_p60');
SELECT col_is_pk('partman_test', 'id_taptest_table_p70', ARRAY['col1'], 'Check for primary key in id_taptest_table_p70');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p60', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p60');
SELECT col_isnt_fk('partman_test', 'id_taptest_table_p70', 'col2', 'Check that foreign key was NOT inherited to id_taptest_table_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p70');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p60', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p60');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p70', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_p60', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p70');

INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(200,210));
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.id_taptest_table');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_p0', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_p10', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_p20', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_p30', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_p40', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_p50', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_p60', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_p70');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p0', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p0');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p10', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p10');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p20', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p20');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p30', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p30');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p40', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p40');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p50', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p50');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p60', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p60');
SELECT table_owner_is ('partman_test', 'id_taptest_table_p70', 'partman_owner', 'Check that ownership change worked for id_taptest_table_p70');

-- TODO make these work
-- Max value is 38 above
SELECT drop_partition_id('partman_test.id_taptest_table', '20', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 doesn''t exists anymore');

UPDATE part_config SET retention = '10' WHERE parent_table = 'partman_test.id_taptest_table';
SELECT drop_partition_id('partman_test.id_taptest_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 doesn''t exists anymore');
SELECT has_table('partman_retention_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 got moved to new schema');
-- Make above work

-- Has to run twice because second time around is when it sees the partition is empty & drops it
SELECT undo_partition_id('partman_test.id_taptest_table', 2, p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 does not exist');

SELECT undo_partition_id('partman_test.id_taptest_table', 10);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[30], 'Check count from parent table after undo');
SELECT has_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p30', 'Check child table had its data removed id_taptest_table_p30');
SELECT has_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p40', 'Check child table had its data removed id_taptest_table_p40');
SELECT has_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p50', 'Check child table had its data removed id_taptest_table_p50');
SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p60', 'Check child table had its data removed id_taptest_table_p60');
SELECT has_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p70', 'Check child table had its data removed id_taptest_table_p70');


SELECT * FROM finish();
ROLLBACK;
