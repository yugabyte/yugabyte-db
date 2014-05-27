-- ########## ID DYNAMIC TESTS ##########
-- Other tests: Single column Foreign Key

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(113);
CREATE SCHEMA partman_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE TABLE partman_test.id_dynamic_table (
    col1 int primary key
    , col2 text not null default 'stuff' references partman_test.fk_test_reference (col2)
    , col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_dynamic_table (col1) VALUES (generate_series(1,9));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_dynamic_table TO partman_basic;
GRANT ALL ON partman_test.id_dynamic_table TO partman_revoke;

SELECT create_parent('partman_test.id_dynamic_table', 'col1', 'id-dynamic', '10', p_use_run_maintenance := true);
SELECT has_table('partman_test', 'id_dynamic_table_p0', 'Check id_dynamic_table_p0 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p10', 'Check id_dynamic_table_p10 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p20', 'Check id_dynamic_table_p20 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p30', 'Check id_dynamic_table_p30 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p40', 'Check id_dynamic_table_p40 exists');
SELECT hasnt_table('partman_test', 'id_dynamic_table_p50', 'Check id_dynamic_table_p50 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p0', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p0');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p10', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p10');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p20', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p20');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p30', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p30');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p40', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_dynamic_table_p40');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p0', 'col2', 'Check that id_dynamic_table_p0 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p10', 'col2', 'Check that id_dynamic_table_p10 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p20', 'col2', 'Check that id_dynamic_table_p20 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p30', 'col2', 'Check that id_dynamic_table_p30 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p40', 'col2', 'Check that id_dynamic_table_p40 inherited foreign key');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_dynamic_table'')::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_dynamic_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table_p0', ARRAY[9], 'Check count from id_dynamic_table_p0');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_dynamic_table FROM partman_revoke;
INSERT INTO partman_test.id_dynamic_table (col1) VALUES (generate_series(10,25));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_dynamic_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table_p10', ARRAY[10], 'Check count from id_dynamic_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table_p20', ARRAY[6], 'Check count from id_dynamic_table_p20');

SELECT hasnt_table('partman_test', 'id_dynamic_table_p50', 'Check id_dynamic_table_p50 doesn''t exists yet');
SELECT hasnt_table('partman_test', 'id_dynamic_table_p60', 'Check id_dynamic_table_p60 doesn''t exists yet');

SELECT run_maintenance();

SELECT has_table('partman_test', 'id_dynamic_table_p50', 'Check id_dynamic_table_p50 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p60', 'Check id_dynamic_table_p60 exists');
SELECT hasnt_table('partman_test', 'id_dynamic_table_p70', 'Check id_dynamic_table_p70 doesn''t exist yet');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p50', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p50');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p50', 'col2', 'Check that id_dynamic_table_p50 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p60', 'col2', 'Check that id_dynamic_table_p60 inherited foreign key');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p50');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_dynamic_table_p50');

GRANT DELETE ON partman_test.id_dynamic_table TO partman_basic;
REVOKE ALL ON partman_test.id_dynamic_table FROM partman_revoke;
ALTER TABLE partman_test.id_dynamic_table OWNER TO partman_owner;
INSERT INTO partman_test.id_dynamic_table (col1) VALUES (generate_series(26,38));
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_dynamic_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table', ARRAY[38], 'Check count from id_dynamic_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table_p20', ARRAY[10], 'Check count from id_dynamic_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_dynamic_table_p30', ARRAY[9], 'Check count from id_dynamic_table_p30');

SELECT has_table('partman_test', 'id_dynamic_table_p60', 'Check id_dynamic_table_p60 exists');
SELECT has_table('partman_test', 'id_dynamic_table_p70', 'Check id_dynamic_table_p70 exists');
SELECT hasnt_table('partman_test', 'id_dynamic_table_p80', 'Check id_dynamic_table_p80 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p60', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p60');
SELECT col_is_pk('partman_test', 'id_dynamic_table_p70', ARRAY['col1'], 'Check for primary key in id_dynamic_table_p70');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p60', 'col2', 'Check that id_dynamic_table_p60 inherited foreign key');
SELECT col_is_fk('partman_test', 'id_dynamic_table_p70', 'col2', 'Check that id_dynamic_table_p60 inherited foreign key');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p50');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_dynamic_table_p50');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_dynamic_table_p60');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p60', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_dynamic_table_p60');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p70');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p70', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p70');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p70');

INSERT INTO partman_test.id_dynamic_table (col1) VALUES (generate_series(200,210));
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_dynamic_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.id_dynamic_table');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p50');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p60');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_dynamic_table_p70');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p0', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p0');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p10', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p10');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p20', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p20');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p30', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p30');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p40', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p40');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p50', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p50');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p60', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p60');
SELECT table_privs_are('partman_test', 'id_dynamic_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_dynamic_table_p70');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p0', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p0');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p10', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p10');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p20', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p20');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p30', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p30');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p40', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p40');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p50', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p50');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p60', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p60');
SELECT table_owner_is ('partman_test', 'id_dynamic_table_p70', 'partman_owner', 'Check that ownership change worked for id_dynamic_table_p70');

SELECT undo_partition_id('partman_test.id_dynamic_table', 10);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_dynamic_table', ARRAY[49], 'Check count from parent table after undo');
SELECT has_table('partman_test', 'id_dynamic_table_p0', 'Check id_dynamic_table_p0 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p0', 'Check child table had its data removed id_dynamic_table_p0');
SELECT has_table('partman_test', 'id_dynamic_table_p10', 'Check id_dynamic_table_p10 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p10', 'Check child table had its data removed id_dynamic_table_p10');
SELECT has_table('partman_test', 'id_dynamic_table_p20', 'Check id_dynamic_table_p20 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p20', 'Check child table had its data removed id_dynamic_table_p20');
SELECT has_table('partman_test', 'id_dynamic_table_p30', 'Check id_dynamic_table_p30 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p30', 'Check child table had its data removed id_dynamic_table_p30');
SELECT has_table('partman_test', 'id_dynamic_table_p40', 'Check id_dynamic_table_p40 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p40', 'Check child table had its data removed id_dynamic_table_p40');
SELECT has_table('partman_test', 'id_dynamic_table_p50', 'Check id_dynamic_table_p50 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p50', 'Check child table had its data removed id_dynamic_table_p50');
SELECT has_table('partman_test', 'id_dynamic_table_p60', 'Check id_dynamic_table_p60 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p60', 'Check child table had its data removed id_dynamic_table_p60');
SELECT has_table('partman_test', 'id_dynamic_table_p70', 'Check id_dynamic_table_p70 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_dynamic_table_p70', 'Check child table had its data removed id_dynamic_table_p70');


SELECT * FROM finish();
ROLLBACK;

