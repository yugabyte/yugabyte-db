-- ########## ID DYNAMIC TESTS ##########
-- Additional tests: turn off pg_jobmon logging, UNLOGGED, PUBLIC role, start with higher number, inherit FK, inherit privileges
    -- Test using a pre-created template table and passing to create_parent. Should allow indexes to be made for initial children.
    -- Tests that foreign keys and normal indexes for PG10 use the template and for PG11 they use the parent. Also since this is id partitioning, we can use the partition key for primary key, so that should work from parent on PG11 as well.

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(120);
CREATE SCHEMA "Partman_Test";
CREATE SCHEMA "Partman_Retention_Test";
CREATE ROLE "Partman_Basic";
CREATE ROLE "Partman_Revoke";
CREATE ROLE "Partman_Owner";

CREATE TABLE "Partman_Test"."Fk_Test_Reference" ("Col2" text unique not null);
INSERT INTO "Partman_Test"."Fk_Test_Reference" VALUES ('stuff');

CREATE UNLOGGED TABLE "Partman_Test"."ID_Taptest_Table" (
    "Col1" bigint 
    , "Col2" text not null default 'stuff'
    , "Col3" timestamptz DEFAULT now()
    , "Col4" text) PARTITION BY RANGE ("Col1");
CREATE TABLE "Partman_Test"."Undo_Taptest" (LIKE "Partman_Test"."ID_Taptest_Table" INCLUDING ALL);
GRANT SELECT,INSERT,UPDATE ON "Partman_Test"."ID_Taptest_Table" TO "Partman_Basic", PUBLIC;
GRANT ALL ON "Partman_Test"."ID_Taptest_Table" TO "Partman_Revoke";
-- Template table
CREATE UNLOGGED TABLE "Partman_Test"."Template_ID_Taptest_Table" (LIKE "Partman_Test"."ID_Taptest_Table");

DO $pg11_objects_check$
BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
    -- Create on parent table
    ALTER TABLE "Partman_Test"."ID_Taptest_Table" ADD PRIMARY KEY ("Col1");
    ALTER TABLE "Partman_Test"."ID_Taptest_Table" ADD FOREIGN KEY ("Col2") REFERENCES "Partman_Test"."Fk_Test_Reference"("Col2");
    CREATE INDEX ON "Partman_Test"."ID_Taptest_Table" ("Col3");
ELSE
    -- Create on template table
    ALTER TABLE "Partman_Test"."Template_ID_Taptest_Table" ADD PRIMARY KEY ("Col1");
    ALTER TABLE "Partman_Test"."Template_ID_Taptest_Table" ADD FOREIGN KEY ("Col2") REFERENCES "Partman_Test"."Fk_Test_Reference"("Col2");
END IF;
END $pg11_objects_check$;

-- Always create the index on teh template also so that we can test excluding duplicates.
CREATE INDEX ON "Partman_Test"."Template_ID_Taptest_Table" ("Col3");

-- Regular unique indexes do not work on native in PG11 if the partition key isn't included
CREATE UNIQUE INDEX ON "Partman_Test"."Template_ID_Taptest_Table" ("Col4");

SELECT create_parent('Partman_Test.ID_Taptest_Table', 'Col1', 'native', '10', p_jobmon := false, p_start_partition := '3000000000', p_template_table := 'Partman_Test.Template_ID_Taptest_Table');
UPDATE part_config SET inherit_privileges = TRUE;
SELECT reapply_privileges('Partman_Test.ID_Taptest_Table');

INSERT INTO "Partman_Test"."ID_Taptest_Table" ("Col1", "Col4") VALUES (generate_series(3000000001,3000000009), 'stuff'||generate_series(3000000001,3000000009));

SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Check ID_Taptest_Table_p3000000000 exists');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Check ID_Taptest_Table_p3000000010 exists');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Check ID_Taptest_Table_p3000000020 exists');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Check ID_Taptest_Table_p3000000030 exists');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Check ID_Taptest_Table_p3000000040 exists');
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Check ID_Taptest_Table_p3000000050 doesn''t exists yet');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000000', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000000');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000010', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000010');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000020', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000020');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000030', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000030');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000040', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000040');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000000');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000010');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000020');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000030');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000040');
-- is_indexed is broken for mixed case so have to manually mark as identity objects https://github.com/theory/pgtap/issues/247
SELECT is_indexed('Partman_Test', 'ID_Taptest_Table_p3000000000', '"Col4"', 'Check that unique index was inherited to ID_Taptest_Table_p3000000000');
SELECT is_indexed('Partman_Test', 'ID_Taptest_Table_p3000000010', '"Col4"', 'Check that unique index was inherited to ID_Taptest_Table_p3000000010');
SELECT is_indexed('Partman_Test', 'ID_Taptest_Table_p3000000020', '"Col4"', 'Check that unique index was inherited to ID_Taptest_Table_p3000000020');
SELECT is_indexed('Partman_Test', 'ID_Taptest_Table_p3000000030', '"Col4"', 'Check that unique index was inherited to ID_Taptest_Table_p3000000030');
SELECT is_indexed('Partman_Test', 'ID_Taptest_Table_p3000000040', '"Col4"', 'Check that unique index was inherited to ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Revoke', ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000040');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table"''::regclass', ARRAY['u'], 'Check that parent table is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000000"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000000 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000010"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000010 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000020"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000020 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000030"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000030 is unlogged');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000040"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000040 is unlogged');

SELECT is_empty('SELECT * FROM ONLY "Partman_Test"."ID_Taptest_Table"', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table"', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table_p3000000000"', ARRAY[9], 'Check count from ID_Taptest_Table_p3000000000');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON "Partman_Test"."ID_Taptest_Table" FROM "Partman_Revoke", PUBLIC;
SELECT run_maintenance();
INSERT INTO "Partman_Test"."ID_Taptest_Table" ("Col1", "Col4") VALUES (generate_series(3000000010,3000000025), 'stuff'||generate_series(3000000010,3000000025));
-- Run again to make new partition based on latest data
SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY "Partman_Test"."ID_Taptest_Table"', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table_p3000000010"', ARRAY[10], 'Check count from ID_Taptest_Table_p3000000010');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table_p3000000020"', ARRAY[6], 'Check count from ID_Taptest_Table_p3000000020');

SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Check ID_Taptest_Table_p3000000050 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000050"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000050 is unlogged');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Check ID_Taptest_Table_p3000000060 exists yet');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000060"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000060 is unlogged');
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Check ID_Taptest_Table_p3000000070 doesn''t exists yet');
--SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000050', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000050');
--SELECT col_isnt_fk('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Col2', 'Check that foreign key was NOT inherited to ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Revoke', ARRAY['SELECT'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000060');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Revoke', ARRAY['SELECT'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000060');

GRANT DELETE ON "Partman_Test"."ID_Taptest_Table" TO "Partman_Basic";
REVOKE ALL ON "Partman_Test"."ID_Taptest_Table" FROM "Partman_Revoke", PUBLIC;
ALTER TABLE "Partman_Test"."ID_Taptest_Table" OWNER TO "Partman_Owner";
INSERT INTO "Partman_Test"."ID_Taptest_Table" ("Col1", "Col4") VALUES (generate_series(3000000026,3000000038), 'stuff'||generate_series(3000000026,3000000038));

SELECT run_maintenance();

SELECT is_empty('SELECT * FROM ONLY "Partman_Test"."ID_Taptest_Table"', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table"', ARRAY[38], 'Check count from id_taptest_table');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table_p3000000020"', ARRAY[10], 'Check count from ID_Taptest_Table_p3000000020');
SELECT results_eq('SELECT count(*)::int FROM "Partman_Test"."ID_Taptest_Table_p3000000030"', ARRAY[9], 'Check count from ID_Taptest_Table_p3000000030');

SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Check ID_Taptest_Table_p3000000070 exists');
SELECT results_eq('SELECT relpersistence::text FROM pg_catalog.pg_class WHERE oid::regclass = ''"Partman_Test"."ID_Taptest_Table_p3000000070"''::regclass', ARRAY['u'], 'Check that ID_Taptest_Table_p3000000070 is unlogged');
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000080', 'Check ID_Taptest_Table_p3000000080 doesn''t exists yet');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000060', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000060');
SELECT col_is_pk('Partman_Test', 'ID_Taptest_Table_p3000000070', ARRAY['Col1'], 'Check for primary key in ID_Taptest_Table_p3000000070');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000060');
SELECT col_is_fk('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Col2', 'Check that foreign key was inherited to ID_Taptest_Table_p3000000070');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Revoke', ARRAY['SELECT'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000060');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Revoke', ARRAY['SELECT'], 'Check Partman_Revoke privileges of ID_Taptest_Table_p3000000060');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000070');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000070');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000070');

-- Requires default table option
--INSERT INTO "Partman_Test"."ID_Taptest_Table" ("Col1", "Col4") VALUES (generate_series(3000000200,3000000210), 'stuff'||generate_series(3000000200,3000000210));
--SELECT run_maintenance();
--SELECT results_eq('SELECT count(*)::int FROM ONLY "Partman_Test"."ID_Taptest_Table"', ARRAY[11], 'Check that data outside trigger scope goes to parent');

SELECT reapply_privileges('Partman_Test.ID_Taptest_Table');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000060');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Basic', ARRAY['SELECT','INSERT','UPDATE','DELETE'], 'Check Partman_Basic privileges of ID_Taptest_Table_p3000000070');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000000');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000010');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000020');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000030');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000040');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000050');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000060');
SELECT table_privs_are('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Revoke', '{}'::text[], 'Check Partman_Revoke has no privileges on ID_Taptest_Table_p3000000070');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000000');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000010');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000020');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000030');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000040');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000050');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000060');
SELECT table_owner_is ('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Partman_Owner', 'Check that ownership change worked for ID_Taptest_Table_p3000000070');

-- Max value is 38 above
SELECT drop_partition_id('Partman_Test.ID_Taptest_Table', '20', p_keep_table := false);
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000000', 'Check ID_Taptest_Table_p3000000000 doesn''t exists anymore');

UPDATE part_config SET retention = '10' WHERE parent_table = 'Partman_Test.ID_Taptest_Table';
SELECT drop_partition_id('Partman_Test.ID_Taptest_Table', p_retention_schema := 'Partman_Retention_Test');
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000010', 'Check ID_Taptest_Table_p3000000010 doesn''t exists anymore');
SELECT has_table('Partman_Retention_Test', 'ID_Taptest_Table_p3000000010', 'Check ID_Taptest_Table_p3000000010 got moved to new schema');

SELECT undo_partition('Partman_Test.ID_Taptest_Table', p_target_table := 'Partman_Test.Undo_Taptest', p_keep_table := false);
SELECT hasnt_table('Partman_Test', 'ID_Taptest_Table_p3000000020', 'Check ID_Taptest_Table_p3000000020 does not exist');

-- Test keeping the rest of the tables
SELECT undo_partition('Partman_Test.ID_Taptest_Table', 10, p_target_table := 'Partman_Test.Undo_Taptest');
SELECT results_eq('SELECT count(*)::int FROM ONLY "Partman_Test"."Undo_Taptest"', ARRAY[19], 'Check count from parent table after undo');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000030', 'Check ID_Taptest_Table_p3000000030 still exists');
SELECT is_empty('SELECT * FROM "Partman_Test"."ID_Taptest_Table_p3000000030"', 'Check child table had its data removed ID_Taptest_Table_p3000000030');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000040', 'Check ID_Taptest_Table_p3000000040 still exists');
SELECT is_empty('SELECT * FROM "Partman_Test"."ID_Taptest_Table_p3000000040"', 'Check child table had its data removed ID_Taptest_Table_p3000000040');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000050', 'Check ID_Taptest_Table_p3000000050 still exists');
SELECT is_empty('SELECT * FROM "Partman_Test"."ID_Taptest_Table_p3000000050"', 'Check child table had its data removed ID_Taptest_Table_p3000000050');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000060', 'Check ID_Taptest_Table_p3000000060 still exists');
SELECT is_empty('SELECT * FROM "Partman_Test"."ID_Taptest_Table_p3000000060"', 'Check child table had its data removed ID_Taptest_Table_p3000000060');
SELECT has_table('Partman_Test', 'ID_Taptest_Table_p3000000070', 'Check ID_Taptest_Table_p3000000070 still exists');
SELECT is_empty('SELECT * FROM "Partman_Test"."ID_Taptest_Table_p3000000070"', 'Check child table had its data removed ID_Taptest_Table_p3000000070');

SELECT hasnt_table('Partman_Test', 'template_id_taptest_table', 'Check that template table was dropped');

SELECT * FROM finish();
ROLLBACK;
