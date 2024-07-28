-- ########## ID DYNAMIC TESTS ##########
-- Additional tests: inherit FK, nonsupuseruser
    -- Test using a pre-created template table and passing to create_parent. Should allow indexes to be made for initial children.
    -- Tests that foreign keys and normal indexes for PG10 use the template and for PG11 they use the parent. Also since this is id partitioning, we can use the partition key for primary key, so that should work from parent on PG11 as well.

-- NOTE: THIS TEST ONLY WORKS FOR PG11+ due to requiring tests for default partition.
-- NOTE: THIS FILE MUST BE RUN AS partman_owner AND CONNECT TO THE DATABASE THAT RAN PART 1 TO EFFECTIVLELY TEST AS NONSUPERUSER
--      Ex  pg_prove -ovf -U partman_owner -d mydb test/test_native/test_nonsuperuser/test-id-nonsuperuser-part2.sql


\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);


DO $pg11_check$
BEGIN
IF current_setting('server_version_num')::int < 110000 THEN
    RAISE EXCEPTION 'This test can only be run on PG11+';
END IF;
END $pg11_check$;

SELECT plan(58);

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

CREATE TABLE partman_test.id_taptest_table (
    col1 bigint 
    , col2 text not null default 'stuff'
    , col3 timestamptz DEFAULT now()
    , col4 text) PARTITION BY RANGE (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.id_taptest_table INCLUDING ALL);
-- Template table
CREATE TABLE partman_test.template_id_taptest_table (LIKE partman_test.id_taptest_table);
-- Regular unique indexes do not work on native in PG11 if the partition key isn't included
CREATE UNIQUE INDEX ON partman_test.template_id_taptest_table (col4);
GRANT SELECT,INSERT,UPDATE ON partman_test.id_taptest_table TO partman_basic, PUBLIC;
GRANT ALL ON partman_test.id_taptest_table TO partman_revoke;

-- Create on parent table since partition column is pk
ALTER TABLE partman_test.id_taptest_table ADD PRIMARY KEY (col1);
ALTER TABLE partman_test.id_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);
CREATE INDEX ON partman_test.id_taptest_table (col3);


SELECT create_parent('partman_test.id_taptest_table', 'col1', 'native', '10', p_jobmon := false, p_template_table := 'partman_test.template_id_taptest_table');
UPDATE part_config SET inherit_privileges = TRUE;

SELECT reapply_privileges('partman_test.id_taptest_table');

INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(1,9), 'stuff'||generate_series(1,9));

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
SELECT col_is_fk('partman_test', 'id_taptest_table_p0', 'col2', 'Check that foreign key was inherited to id_taptest_table_p0');
SELECT col_is_fk('partman_test', 'id_taptest_table_p10', 'col2', 'Check that foreign key was inherited to id_taptest_table_p10');
SELECT col_is_fk('partman_test', 'id_taptest_table_p20', 'col2', 'Check that foreign key was inherited to id_taptest_table_p20');
SELECT col_is_fk('partman_test', 'id_taptest_table_p30', 'col2', 'Check that foreign key was inherited to id_taptest_table_p30');
SELECT col_is_fk('partman_test', 'id_taptest_table_p40', 'col2', 'Check that foreign key was inherited to id_taptest_table_p40');

SELECT table_privs_are('partman_test', 'id_taptest_table', 'partman_basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check partman_basic privileges of id_taptest_table');
SELECT table_privs_are('partman_test', 'id_taptest_table', 'partman_revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check partman_revoke privileges of id_taptest_table');

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from id_taptest_table_p0');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON partman_test.id_taptest_table FROM partman_revoke, PUBLIC;

SELECT run_maintenance();
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(10,25), 'stuff'||generate_series(10,25));
-- Run again to make new partition based on latest data
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[6], 'Check count from id_taptest_table_p20');

SELECT has_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 exists');
SELECT has_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 exists yet');
SELECT hasnt_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p50', ARRAY['col1'], 'Check for primary key in id_taptest_table_p50');
SELECT col_is_fk('partman_test', 'id_taptest_table_p50', 'col2', 'Check that foreign key was inherited to id_taptest_table_p50');

SELECT table_privs_are('partman_test', 'id_taptest_table', 'partman_revoke', ARRAY['SELECT'], 'Check new partman_revoke privileges of id_taptest_table');

INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(26,38), 'stuff'||generate_series(26,38));

SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[10], 'Check count from id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p30', ARRAY[9], 'Check count from id_taptest_table_p30');

SELECT has_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p80', 'Check id_taptest_table_p80 doesn''t exists yet');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60', ARRAY['col1'], 'Check for primary key in id_taptest_table_p60');
SELECT col_is_pk('partman_test', 'id_taptest_table_p70', ARRAY['col1'], 'Check for primary key in id_taptest_table_p70');
SELECT col_is_fk('partman_test', 'id_taptest_table_p60', 'col2', 'Check that foreign key was inherited to id_taptest_table_p60');
SELECT col_is_fk('partman_test', 'id_taptest_table_p70', 'col2', 'Check that foreign key was inherited to id_taptest_table_p70');


-- Requires default table option
INSERT INTO partman_test.id_taptest_table (col1, col4) VALUES (generate_series(200,210), 'stuff'||generate_series(200,210));
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[49], 'Check that data outside child scope is visible from parent');
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.id_taptest_table_default', ARRAY[11], 'Check that data outside child scope goes to default');
SELECT partition_data_id('partman_test.id_taptest_table', 10);

SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_default', 'Check data was removed from default');
SELECT has_table('partman_test', 'id_taptest_table_p200', 'Check id_taptest_table_p200 exists');
SELECT has_table('partman_test', 'id_taptest_table_p210', 'Check id_taptest_table_p210 exists');
SELECT run_maintenance();



-- Test keeping the rest of the tables
SELECT undo_partition('partman_test.id_taptest_table', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.undo_taptest', ARRAY[49], 'Check count from undo target table after undo');


SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p200', 'Check id_taptest_table_p200 doesn''t exists anymore');
SELECT hasnt_table('partman_test', 'id_taptest_table_p210', 'Check id_taptest_table_p210 doesn''t exists anymore');

SELECT hasnt_table('partman_test', 'template_id_taptest_table', 'Check that template table was dropped');
SELECT hasnt_table('partman_test', 'id_taptest_table_default', 'Check that default table was dropped');

SELECT * FROM finish();
ROLLBACK;
