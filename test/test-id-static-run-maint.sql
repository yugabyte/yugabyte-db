-- ########## ID STATIC TESTS ##########
-- Other tests: additional constraints single column, use run_maintenance(), with OIDs

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(126);
CREATE SCHEMA partman_test;
CREATE SCHEMA partman_retention_test;
CREATE ROLE partman_basic;
CREATE ROLE partman_revoke;
CREATE ROLE partman_owner;

CREATE TABLE partman_test.id_static_table (col1 int primary key, col2 text, col3 timestamptz DEFAULT now()) WITH (OIDS);
INSERT INTO partman_test.id_static_table (col1) VALUES (generate_series(1,9));
GRANT SELECT,INSERT,UPDATE ON partman_test.id_static_table TO partman_basic;
GRANT ALL ON partman_test.id_static_table TO partman_revoke;

SELECT create_parent('partman_test.id_static_table', 'col1', 'id-static', '10', '{"col3"}', p_use_run_maintenance := true);
SELECT has_table('partman_test', 'id_static_table_p0', 'Check id_static_table_p0 exists');
SELECT has_table('partman_test', 'id_static_table_p10', 'Check id_static_table_p10 exists');
SELECT has_table('partman_test', 'id_static_table_p20', 'Check id_static_table_p20 exists');
SELECT has_table('partman_test', 'id_static_table_p30', 'Check id_static_table_p30 exists');
SELECT has_table('partman_test', 'id_static_table_p40', 'Check id_static_table_p40 exists');
SELECT hasnt_table('partman_test', 'id_static_table_p50', 'Check id_static_table_p50 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_static_table_p0', ARRAY['col1'], 'Check for primary key in id_static_table_p0');
SELECT col_is_pk('partman_test', 'id_static_table_p10', ARRAY['col1'], 'Check for primary key in id_static_table_p10');
SELECT col_is_pk('partman_test', 'id_static_table_p20', ARRAY['col1'], 'Check for primary key in id_static_table_p20');
SELECT col_is_pk('partman_test', 'id_static_table_p30', ARRAY['col1'], 'Check for primary key in id_static_table_p30');
SELECT col_is_pk('partman_test', 'id_static_table_p40', ARRAY['col1'], 'Check for primary key in id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_static_table_p40');

SELECT results_eq('SELECT partition_data_id(''partman_test.id_static_table'')::int', ARRAY[9], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_static_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p0', ARRAY[9], 'Check count from id_static_table_p0');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_static_table FROM partman_revoke;
INSERT INTO partman_test.id_static_table (col1) VALUES (generate_series(10,25));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_static_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p10', ARRAY[10], 'Check count from id_static_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p20', ARRAY[6], 'Check count from id_static_table_p20');

SELECT hasnt_table('partman_test', 'id_static_table_p50', 'Check id_static_table_p50 doesn''t exist yet');
SELECT hasnt_table('partman_test', 'id_static_table_p60', 'Check id_static_table_p60 doesn''t exist yet');

SELECT run_maintenance();

SELECT has_table('partman_test', 'id_static_table_p50', 'Check id_static_table_p50 exists');
SELECT has_table('partman_test', 'id_static_table_p60', 'Check id_static_table_p60 doesn''t exist yet');
SELECT hasnt_table('partman_test', 'id_static_table_p70', 'Check id_static_table_p70 doesn''t exist yet');
SELECT col_is_pk('partman_test', 'id_static_table_p50', ARRAY['col1'], 'Check for primary key in id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_static_table_p50');

GRANT DELETE ON partman_test.id_static_table TO partman_basic;
REVOKE ALL ON partman_test.id_static_table FROM partman_revoke;
ALTER TABLE partman_test.id_static_table OWNER TO partman_owner;
INSERT INTO partman_test.id_static_table (col1) VALUES (generate_series(26,60));
SELECT run_maintenance();


SELECT is_empty('SELECT * FROM ONLY partman_test.id_static_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table', ARRAY[60], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p20', ARRAY[10], 'Check count from id_static_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p30', ARRAY[10], 'Check count from id_static_table_p30');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p40', ARRAY[10], 'Check count from id_static_table_p40');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_static_table_p50', ARRAY[10], 'Check count from id_static_table_p50');

-- Check for additional constraint on date column
SELECT col_has_check('partman_test', 'id_static_table_p0', 'col3', 'Check for additional constraint on col3 on id_static_table_p0');

SELECT has_table('partman_test', 'id_static_table_p60', 'Check id_static_table_p60 exists');
SELECT has_table('partman_test', 'id_static_table_p70', 'Check id_static_table_p70 exists');
SELECT has_table('partman_test', 'id_static_table_p80', 'Check id_static_table_p80 exists');
SELECT has_table('partman_test', 'id_static_table_p90', 'Check id_static_table_p90 exists');
SELECT has_table('partman_test', 'id_static_table_p100', 'Check id_static_table_p100 exists');
SELECT hasnt_table('partman_test', 'id_static_table_p110', 'Check id_static_table_p110 doesn''t exist yet');
SELECT col_is_pk('partman_test', 'id_static_table_p60', ARRAY['col1'], 'Check for primary key in id_static_table_p60');
SELECT col_is_pk('partman_test', 'id_static_table_p70', ARRAY['col1'], 'Check for primary key in id_static_table_p70');
SELECT col_is_pk('partman_test', 'id_static_table_p80', ARRAY['col1'], 'Check for primary key in id_static_table_p80');
SELECT col_is_pk('partman_test', 'id_static_table_p90', ARRAY['col1'], 'Check for primary key in id_static_table_p90');
SELECT col_is_pk('partman_test', 'id_static_table_p100', ARRAY['col1'], 'Check for primary key in id_static_table_p100');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_static_table_p60');
SELECT table_privs_are('partman_test', 'id_static_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p70');
SELECT table_privs_are('partman_test', 'id_static_table_p80', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p80');
SELECT table_privs_are('partman_test', 'id_static_table_p90', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p90');
SELECT table_privs_are('partman_test', 'id_static_table_p100', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p100');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke privileges of id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p60', 'partman_revoke', ARRAY['SELECT'], 'Check partman_revoke has no privileges on id_static_table_p60');
SELECT table_privs_are('partman_test', 'id_static_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p70');
SELECT table_privs_are('partman_test', 'id_static_table_p80', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p80');
SELECT table_privs_are('partman_test', 'id_static_table_p90', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p90');
SELECT table_privs_are('partman_test', 'id_static_table_p100', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p100');
SELECT table_owner_is ('partman_test', 'id_static_table_p70', 'partman_owner', 'Check that ownership change worked for id_static_table_p70');
SELECT table_owner_is ('partman_test', 'id_static_table_p80', 'partman_owner', 'Check that ownership change worked for id_static_table_p80');
SELECT table_owner_is ('partman_test', 'id_static_table_p90', 'partman_owner', 'Check that ownership change worked for id_static_table_p90');
SELECT table_owner_is ('partman_test', 'id_static_table_p100', 'partman_owner', 'Check that ownership change worked for id_static_table_p100');

INSERT INTO partman_test.id_static_table (col1) VALUES (generate_series(200,210));
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_static_table', ARRAY[11], 'Check that data outside trigger scope goes to parent');
-- Remove rows in parent so drop function test can work properly
DELETE FROM ONLY partman_test.id_static_table;

SELECT reapply_privileges('partman_test.id_static_table');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p60', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p60');
SELECT table_privs_are('partman_test', 'id_static_table_p70', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p70');
SELECT table_privs_are('partman_test', 'id_static_table_p80', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p80');
SELECT table_privs_are('partman_test', 'id_static_table_p90', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p90');
SELECT table_privs_are('partman_test', 'id_static_table_p100', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check partman_basic privileges of id_static_table_p100');
SELECT table_privs_are('partman_test', 'id_static_table_p0', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p0');
SELECT table_privs_are('partman_test', 'id_static_table_p10', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p10');
SELECT table_privs_are('partman_test', 'id_static_table_p20', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p20');
SELECT table_privs_are('partman_test', 'id_static_table_p30', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p30');
SELECT table_privs_are('partman_test', 'id_static_table_p40', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p40');
SELECT table_privs_are('partman_test', 'id_static_table_p50', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p50');
SELECT table_privs_are('partman_test', 'id_static_table_p60', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p60');
SELECT table_privs_are('partman_test', 'id_static_table_p70', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p70');
SELECT table_privs_are('partman_test', 'id_static_table_p80', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p80');
SELECT table_privs_are('partman_test', 'id_static_table_p90', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p90');
SELECT table_privs_are('partman_test', 'id_static_table_p100', 'partman_revoke', '{}'::text[], 'Check partman_revoke has no privileges on id_static_table_p100');
SELECT table_owner_is ('partman_test', 'id_static_table_p0', 'partman_owner', 'Check that ownership change worked for id_static_table_p0');
SELECT table_owner_is ('partman_test', 'id_static_table_p10', 'partman_owner', 'Check that ownership change worked for id_static_table_p10');
SELECT table_owner_is ('partman_test', 'id_static_table_p20', 'partman_owner', 'Check that ownership change worked for id_static_table_p20');
SELECT table_owner_is ('partman_test', 'id_static_table_p30', 'partman_owner', 'Check that ownership change worked for id_static_table_p30');
SELECT table_owner_is ('partman_test', 'id_static_table_p40', 'partman_owner', 'Check that ownership change worked for id_static_table_p40');
SELECT table_owner_is ('partman_test', 'id_static_table_p50', 'partman_owner', 'Check that ownership change worked for id_static_table_p50');
SELECT table_owner_is ('partman_test', 'id_static_table_p60', 'partman_owner', 'Check that ownership change worked for id_static_table_p60');
SELECT table_owner_is ('partman_test', 'id_static_table_p70', 'partman_owner', 'Check that ownership change worked for id_static_table_p70');
SELECT table_owner_is ('partman_test', 'id_static_table_p80', 'partman_owner', 'Check that ownership change worked for id_static_table_p80');
SELECT table_owner_is ('partman_test', 'id_static_table_p90', 'partman_owner', 'Check that ownership change worked for id_static_table_p90');
SELECT table_owner_is ('partman_test', 'id_static_table_p100', 'partman_owner', 'Check that ownership change worked for id_static_table_p100');

SELECT drop_partition_id('partman_test.id_static_table', '45', p_keep_table := false);
SELECT hasnt_table('partman_test', 'id_static_table_p0', 'Check id_static_table_p0 doesn''t exists anymore');

UPDATE part_config SET retention = '35' WHERE parent_table = 'partman_test.id_static_table';
SELECT drop_partition_id('partman_test.id_static_table', p_retention_schema := 'partman_retention_test');
SELECT hasnt_table('partman_test', 'id_static_table_p10', 'Check id_static_table_p10 doesn''t exists anymore');
SELECT has_table('partman_retention_test', 'id_static_table_p10', 'Check id_static_table_p10 got moved to new schema');

SELECT undo_partition_id('partman_test.id_static_table', 10, p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_static_table', ARRAY[41], 'Check count from parent table after undo');
SELECT hasnt_table('partman_test', 'id_static_table_p20', 'Check id_static_table_p20 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p30', 'Check id_static_table_p30 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p40', 'Check id_static_table_p40 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p50', 'Check id_static_table_p50 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p60', 'Check id_static_table_p60 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p70', 'Check id_static_table_p70 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p80', 'Check id_static_table_p80 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_static_table_p90', 'Check id_static_table_p90 doesn''t exists anymore');

SELECT * FROM finish();
ROLLBACK;
