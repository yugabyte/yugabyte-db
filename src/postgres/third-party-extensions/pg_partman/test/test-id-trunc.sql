-- ########## ID TESTS ##########
-- Other tests: Long name truncation

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(104);
CREATE SCHEMA partman_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.id_taptest_table_123456789012345678901234567890123456789012345 (col1 int primary key, col2 text, col3 timestamptz DEFAULT now());
INSERT INTO partman_test.id_taptest_table_123456789012345678901234567890123456789012345 (col1) VALUES (generate_series(1,9));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table_123456789012345678901234567890123456789012345 TO partman_basic;
GRANT ALL ON partman_test.id_taptest_table_123456789012345678901234567890123456789012345 TO partman_revoke;

SELECT create_parent('partman_test.id_taptest_table_123456789012345678901234567890123456789012345', 'col1', 'partman', '10');
SELECT has_table('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'Check id_taptest_table_1234567890123456789012345678901234567890123_p0 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'Check id_taptest_table_123456789012345678901234567890123456789012_p10 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'Check id_taptest_table_123456789012345678901234567890123456789012_p20 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'Check id_taptest_table_123456789012345678901234567890123456789012_p30 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'Check id_taptest_table_123456789012345678901234567890123456789012_p40 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'Check id_taptest_table_123456789012345678901234567890123456789012_p50 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', ARRAY['col1'], 'Check for primary key in id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p40');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_taptest_table_123456789012345678901234567890123456789012345'')::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_123456789012345678901234567890123456789012345', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012345', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_1234567890123456789012345678901234567890123_p0', ARRAY[9], 'Check count from id_taptest_table_1234567890123456789012345678901234567890123_p0');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_taptest_table_123456789012345678901234567890123456789012345 FROM partman_revoke;
INSERT INTO partman_test.id_taptest_table_123456789012345678901234567890123456789012345 (col1) VALUES (generate_series(10,25));
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_123456789012345678901234567890123456789012345', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p10', ARRAY[10], 'Check count from id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p20', ARRAY[6], 'Check count from id_taptest_table_123456789012345678901234567890123456789012_p20');

SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'Check id_taptest_table_123456789012345678901234567890123456789012_p50 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'Check id_taptest_table_123456789012345678901234567890123456789012_p60 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'Check id_taptest_table_123456789012345678901234567890123456789012_p70 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p650');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p60');

GRANT DELETE ON partman_test.id_taptest_table_123456789012345678901234567890123456789012345 TO partman_basic;
REVOKE ALL ON partman_test.id_taptest_table_123456789012345678901234567890123456789012345 FROM partman_revoke;
ALTER TABLE partman_test.id_taptest_table_123456789012345678901234567890123456789012345 OWNER TO partman_owner;
INSERT INTO partman_test.id_taptest_table_123456789012345678901234567890123456789012345 (col1) VALUES (generate_series(26,38));
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_123456789012345678901234567890123456789012345', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012345', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p20', ARRAY[10], 'Check count from id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p30', ARRAY[9], 'Check count from id_taptest_table_123456789012345678901234567890123456789012_p30');

SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'Check id_taptest_table_123456789012345678901234567890123456789012_p60 exists');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'Check id_taptest_table_123456789012345678901234567890123456789012_p70 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p80', 'Check id_taptest_table_123456789012345678901234567890123456789012_p80 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT col_is_pk('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', ARRAY['col1'], 'Check for primary key in id_taptest_table_123456789012345678901234567890123456789012_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p70');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p70');

INSERT INTO partman_test.id_taptest_table_123456789012345678901234567890123456789012345 (col1) VALUES (generate_series(200,210));
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table_123456789012345678901234567890123456789012345', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('partman_test.id_taptest_table_123456789012345678901234567890123456789012345');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_taptest_table_123456789012345678901234567890123456789012_p70');
SELECT table_privs_are('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT table_privs_are('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_taptest_table_123456789012345678901234567890123456789012_p70');
SELECT table_owner_is ('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'partman_owner', 'Check that ownership change worked for id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT table_owner_is ('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'partman_owner', 'Check that ownership change worked for id_taptest_table_123456789012345678901234567890123456789012_p70');

SELECT undo_partition('partman_test.id_taptest_table_123456789012345678901234567890123456789012345', 10);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table_123456789012345678901234567890123456789012345', ARRAY[49], 'Check count from parent table after undo');
SELECT has_table('partman_test', 'id_taptest_table_1234567890123456789012345678901234567890123_p0', 'Check id_taptest_table_1234567890123456789012345678901234567890123_p0 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_1234567890123456789012345678901234567890123_p0', 'Check child table had its data removed id_taptest_table_1234567890123456789012345678901234567890123_p0');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p10', 'Check id_taptest_table_123456789012345678901234567890123456789012_p10 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p10', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p10');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p20', 'Check id_taptest_table_123456789012345678901234567890123456789012_p20 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p20', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p20');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p30', 'Check id_taptest_table_123456789012345678901234567890123456789012_p30 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p30', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p30');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p40', 'Check id_taptest_table_123456789012345678901234567890123456789012_p40 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p40', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p40');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p50', 'Check id_taptest_table_123456789012345678901234567890123456789012_p50 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p50', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p50');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p60', 'Check id_taptest_table_123456789012345678901234567890123456789012_p60 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p60', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p60');
SELECT has_table('partman_test', 'id_taptest_table_123456789012345678901234567890123456789012_p70', 'Check id_taptest_table_123456789012345678901234567890123456789012_p70 still exists');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_123456789012345678901234567890123456789012_p70', 'Check child table had its data removed id_taptest_table_123456789012345678901234567890123456789012_p70');


SELECT * FROM finish();
ROLLBACK;
