-- ########## ID TESTS ##########
-- Additional tests: turn off pg_jobmon logging, UNLOGGED, larger than necessary p_batch_count to partition_data_id(), retention, PUBLIC role, upsert with ON CONFLICT DO UPDATE

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(113);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE UNLOGGED TABLE partman_test.id_taptest_table (
    col1 bigint primary key
    , col2 text not null default 'stuff'
    , col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(3000000001,3000000009));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table TO partman_basic, PUBLIC;
GRANT ALL ON partman_test.id_taptest_table TO partman_revoke;

SELECT results_eq('SELECT create_parent(''partman_test.id_taptest_table'', ''col1'', ''partman'', ''10'', p_jobmon := false, p_upsert :=''ON CONFLICT (col1) DO UPDATE SET col2=EXCLUDED.col2, col3=EXCLUDED.col3'')::text', ARRAY['true'], 'Check that create_parent() returns true');
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

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table'', p_batch_count := 5)::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000000', ARRAY[9], 'Check count from id_taptest_table_p3000000000');

-- INSERT original id values again and check their col2 values
INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(3000000001,3000000009), 'stuff_upserted');
SELECT partman.run_maintenance();
SELECT results_eq('SELECT col1, col2 FROM partman_test.id_taptest_table',
    $$VALUES (3000000001, 'stuff_upserted'),
             (3000000002, 'stuff_upserted'),
             (3000000003, 'stuff_upserted'),
             (3000000004, 'stuff_upserted'),
             (3000000005, 'stuff_upserted'),
             (3000000006, 'stuff_upserted'),
             (3000000007, 'stuff_upserted'),
             (3000000008, 'stuff_upserted'),
             (3000000009, 'stuff_upserted')$$,
            'Checking values after upsert trigger');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_taptest_table FROM partman_revoke, PUBLIC;
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(3000000010,3000000025));
SELECT partman.run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000010', ARRAY[10], 'Check count from id_taptest_table_p3000000010');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[6], 'Check count from id_taptest_table_p3000000020');

SELECT has_table('partman_test', 'id_taptest_table_p3000000050', 'Check id_taptest_table_p3000000050 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000050''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000050 is unlogged');
SELECT has_table('partman_test', 'id_taptest_table_p3000000060', 'Check id_taptest_table_p3000000060 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000060''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000060 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000050', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000050');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000060', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000000', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000000');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000010', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000010');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000020', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000020');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000030', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000030');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000040', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000040');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_p3000000060');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000050', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000050');
SELECT table_privs_are('partman_test', 'id_taptest_table_p3000000060', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_p3000000060');

GRANT DELETE ON partman_test.id_taptest_table TO partman_basic;
REVOKE ALL ON partman_test.id_taptest_table FROM partman_revoke, PUBLIC;
ALTER TABLE partman_test.id_taptest_table OWNER TO partman_owner;
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(3000000026,3000000038));
SELECT partman.run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000020', ARRAY[10], 'Check count from id_taptest_table_p3000000020');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p3000000030', ARRAY[9], 'Check count from id_taptest_table_p3000000030');

SELECT has_table('partman_test', 'id_taptest_table_p3000000070', 'Check id_taptest_table_p3000000070 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''partman_test.id_taptest_table_p3000000070''::regclass', ARRAY['u'], 'Check that id_taptest_table_p3000000070 is unlogged');
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000080', 'Check id_taptest_table_p3000000080 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p3000000070', ARRAY['col1'], 'Check for primary key in id_taptest_table_p3000000070');
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

INSERT INTO partman_test.id_taptest_table (col1, col2) VALUES (generate_series(3000000026,3000000038), 'stuff_upserted');
SELECT partman.run_maintenance();
SELECT results_eq('SELECT col1, col2 FROM partman_test.id_taptest_table',
    $$VALUES (3000000001, 'stuff_upserted'),
             (3000000002, 'stuff_upserted'),
             (3000000003, 'stuff_upserted'),
             (3000000004, 'stuff_upserted'),
             (3000000005, 'stuff_upserted'),
             (3000000006, 'stuff_upserted'),
             (3000000007, 'stuff_upserted'),
             (3000000008, 'stuff_upserted'),
             (3000000009, 'stuff_upserted'),
             (3000000010, 'stuff'),
             (3000000011, 'stuff'),
             (3000000012, 'stuff'),
             (3000000013, 'stuff'),
             (3000000014, 'stuff'),
             (3000000015, 'stuff'),
             (3000000016, 'stuff'),
             (3000000017, 'stuff'),
             (3000000018, 'stuff'),
             (3000000019, 'stuff'),
             (3000000020, 'stuff'),
             (3000000021, 'stuff'),
             (3000000022, 'stuff'),
             (3000000023, 'stuff'),
             (3000000024, 'stuff'),
             (3000000025, 'stuff'),
             (3000000026, 'stuff_upserted'),
             (3000000027, 'stuff_upserted'),
             (3000000028, 'stuff_upserted'),
             (3000000029, 'stuff_upserted'),
             (3000000030, 'stuff_upserted'),
             (3000000031, 'stuff_upserted'),
             (3000000032, 'stuff_upserted'),
             (3000000033, 'stuff_upserted'),
             (3000000034, 'stuff_upserted'),
             (3000000035, 'stuff_upserted'),
             (3000000036, 'stuff_upserted'),
             (3000000037, 'stuff_upserted'),
             (3000000038, 'stuff_upserted')$$,
            'Checking values after upsert trigger');


INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(3000000200,3000000210));
SELECT partman.run_maintenance();
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

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

SELECT undo_partition('partman_test.id_taptest_table', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_taptest_table_p3000000020', 'Check id_taptest_table_p3000000020 does not exist');

-- Test keeping the reset of the tables
SELECT undo_partition('partman_test.id_taptest_table', 10);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table', ARRAY[30], 'Check count from parent table after undo');
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


SELECT * FROM finish();
ROLLBACK;
