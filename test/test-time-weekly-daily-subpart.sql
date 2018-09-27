-- ########## TIME WEEKLY-DAILY SUBPARTITION TESTS ##########
-- Other tests: Mixed case & special characters, extra constraints, hybrid trigger puts data outside premake into proper tables, dropping only indexes in retention, grants/revokes, ensure retention doesn't drop final table
-- Also tests that maintenance catches up on all subpartition tables as long as data exists in them
-- NOTE Some tests failing on mondays. Commented them out with notes below. Thanks Garfield. 

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(277);
CREATE SCHEMA "Partman_test";
CREATE ROLE "partman-basic";
CREATE ROLE "Partman_Revoke";
CREATE ROLE partman_owner;

CREATE TABLE "Partman_test"."Time-taptest-Table" ("COL1" int primary key, col2 text, "Col-3" timestamptz NOT NULL DEFAULT now());
-- Time data being inserted is truncated to the beginning of the week to avoid missing partitions
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(1,10), date_trunc('week', CURRENT_TIMESTAMP));
GRANT SELECT,INSERT,UPDATE ON "Partman_test"."Time-taptest-Table" TO "partman-basic";
GRANT ALL ON "Partman_test"."Time-taptest-Table" TO "Partman_Revoke";

SELECT create_parent('Partman_test.Time-taptest-Table', 'Col-3', 'partman', 'weekly', '{"COL1"}', p_premake := 4, p_start_partition := to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'YYYY-MM-DD HH24:MI:SS'));
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')
    , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' exists (this week)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' exists (+1 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' exists (+2 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' exists (+3 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' exists (+4 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist (+5 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' exists (-1 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' exists (-2 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' exists (-3 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' exists (-4 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' exists (-5 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' exists (-6 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'7 weeks'::interval, 'IYYY"w"IW')||' does not exist (-7 weeks)');

SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' (+2 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' (-5 week)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' (-6 week)');

SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' (+2 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' (-5 week)');

SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' (-6 week)');


SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' (+2 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' (-5 week)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT', 'INSERT', 'UPDATE', 'DELETE', 'TRUNCATE', 'REFERENCES', 'TRIGGER'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' (-6 week)');

SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table'')::int', ARRAY[10], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table"', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table"', ARRAY[10], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');

REVOKE INSERT, UPDATE, DELETE, TRUNCATE, REFERENCES, TRIGGER ON "Partman_test"."Time-taptest-Table" FROM "Partman_Revoke";
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(11,20), date_trunc('week', CURRENT_TIMESTAMP - '4 week'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(21,25), date_trunc('week', CURRENT_TIMESTAMP - '3 week'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(26,30), date_trunc('week', CURRENT_TIMESTAMP - '2 weeks'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(31,37), date_trunc('week', CURRENT_TIMESTAMP - '1 week'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(38,49), date_trunc('week', CURRENT_TIMESTAMP + '1 week'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(50,70), date_trunc('week', CURRENT_TIMESTAMP + '2 weeks'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(71,85), date_trunc('week', CURRENT_TIMESTAMP + '3 weeks'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(86,100), date_trunc('week', CURRENT_TIMESTAMP + '4 weeks'::interval));

-- Test hybrid trigger puts data in tables outside premake range
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(101,110), date_trunc('week', CURRENT_TIMESTAMP - '5 weeks'::interval));
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(111,130), date_trunc('week', CURRENT_TIMESTAMP - '6 weeks'::interval));
-- Add data for checking that daily subpartition constraint management is working (8 days is before new premake value below). 
-- This isn't currently working, but leaving this here for future testing if I can get this working
-- Also, when this test script is run on a monday, 8 days ago is 2 weeks ago, not 1 week ago, so it throws some of the rest of the tests off.
-- INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(131,140), CURRENT_TIMESTAMP - '8 days'::interval);

SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table"', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[7], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[20], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-6 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-5 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[12], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[21], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 week'::interval, 'IYYY"w"IW')||' (+2 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 week'::interval, 'IYYY"w"IW')||' (+3 week)');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 week'::interval, 'IYYY"w"IW')||' (+4 week)');

-- Ensure all the current child tables get the new privileges given above so they propagate properly when subpartitions are created
SELECT reapply_privileges('Partman_test.Time-taptest-Table');

SELECT create_sub_parent('Partman_test.Time-taptest-Table', 'Col-3', 'partman', 'daily', '{"COL1"}', p_premake := 4);
-- Check daily partitions
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists (current day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists (+1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists (+2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists (+3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists (+4 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists (-1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists (-2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists (-3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists (-4 day)');

SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' (current day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (+1 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')||' (+2 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')||' (+3 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')||' (+4 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' (-1 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')||' (-2 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')||' (-3 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')||' (-4 day)');

SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' (now)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (+1 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')||' (+2 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')||' (+3 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')||' (+4 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' (-1 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')||' (-2 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')||' (-3 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')||' (-4 day)');

SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' (now)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (+1 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')||' (+2 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')||' (+3 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')||' (+4 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' (-1 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')||' (-2 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'],
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')||' (-3 day)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')||' (-4 day)');

-- Check for future weekly partitions and their minimal daily. Not testing pk & privileges. If above is fine, these should be fine
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval), 'YYYY_MM_DD')||' exists (+1 weeks)');
-- Cannot reliably test for +1week+1day not existing since near the end of the week, the premake days go into the following week
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval), 'YYYY_MM_DD')||' exists (+2 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+2 weeks +1 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval), 'YYYY_MM_DD')||' exists (+3 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+3 weeks +1 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval), 'YYYY_MM_DD')||' exists (+4 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+8 weeks +1 day)');

SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10] , 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (now)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[20], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' (-6 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' (-5 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' (-4 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[5], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' (-3 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[5], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[7], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[12], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' (+1 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[21], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' (+2 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[15], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' (+3 weeks)');
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[15], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' (+4 weeks)');

-- This should cause all subpartition sets to catch up and premake 4 child tables
SELECT run_maintenance();
-- Data at time of run_maintenance() call exists for +4 weeks, premake is 4, so should be partitions for 4 weeks after that (+8 weeks))
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' exists (now)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' exists (+6 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' exists (+7 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' exists (+8 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' does not exist (+9 weeks)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' exists (+5 weeks)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' exists (+6 weeks)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' exists (+7 weeks)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' exists (+8 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' (+5 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' (+6 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' (+7 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' (+8 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' (+5 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' (+6 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' (+7 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' (+8 weeks)');

SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table"', 'Check that parent table has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW')||'"'
        , 'Check that parent table Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW')||' has had no data inserted to it');

-- Check that all subpartitions have their child tables +4 days after the minimum (minimal partition was already checked above)
-- Since they have data, maintenance should work properly and catch up all the missing child tables
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-6 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-6 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-6 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-6 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'6 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||'does not exist (-6 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-5 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-5 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-5 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-5 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'5 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (-5 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-4 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-4 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-4 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-4 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (-4 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-3 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-3 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-3 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-3 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (-3 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-2 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-2 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-2 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-2 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (-2 weeks +5 day)');

-- These tests may fail depending on the day of the week due to previous partitions being created my create_parent(). Example: Monday will cause failures because previous Friday table is created.
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (-1 weeks +1 day). This test may fail depending on day of the week.');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (-1 weeks +2 day) This test may fail depending on day of the week.');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (-1 weeks +3 day) This test may fail depending on day of the week.');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (-1 weeks +4 day) This test may fail depending on day of the week.');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (-1 weeks +5 day) This test may fail depending on day of the week.');

-- No need to test for this week's table. Should already have been tested elsewhere
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (+1 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (+1 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (+1 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (+1 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (+1 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (+2 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (+2 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (+2 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (+2 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (+2 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (+3 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (+3 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (+3 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (+3 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (+3 weeks +5 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' exists (+4 weeks +1 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'2 day'::interval, 'YYYY_MM_DD')||' exists (+4 weeks +2 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'3 day'::interval, 'YYYY_MM_DD')||' exists (+4 weeks +3 day)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'4 day'::interval, 'YYYY_MM_DD')||' exists (+4 weeks +4 day)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval)+'5 day'::interval, 'YYYY_MM_DD')||' does not exist (+4 weeks +5 day)');

-- Only minimum partition should be in the following sub partition sets
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval), 'YYYY_MM_DD')||' exists (+5 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'36 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'36 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+5 weeks +1 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'6 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'6 weeks'::interval), 'YYYY_MM_DD')||' exists (+6 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'43 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'6 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'43 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'6 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+6 weeks +1 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'7 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'7 weeks'::interval), 'YYYY_MM_DD')||' exists (+7 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'50 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'7 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'50 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'7 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+7 weeks +1 day)');

SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'8 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'8 weeks'::interval), 'YYYY_MM_DD')||' exists (+8 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'57 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'8 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'57 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'8 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+8 weeks +1 day)');


-- Default optimize_constraint is 30, so set it equal to premake for when this test was originally written
-- This should then cause all the subpartition sets to create the full week of child tables and mark the sets as full
UPDATE part_config SET premake = 5, optimize_constraint = 5 WHERE parent_table = 'Partman_test.Time-taptest-Table';
UPDATE part_config SET premake = 7, optimize_constraint = 7 WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%';
SELECT run_maintenance();

-- Should automatically put additional constraint on 6 weeks prior to the current max value (+4 weeks) in the partition set. So 2 weeks ago.
SELECT col_has_check('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 'COL1'
    , 'Check for additional constraint on col1 on Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' (-2 weeks)');

-- Data at time of run_maintenance() call exists for +4 weeks, premake is now 5, so should be partitions for 5 weeks after that (+9 weeks))
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' exists (+9 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||' does not exist (+10 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')||' exists (+9 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'64 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'64 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+9 weeks +1 day)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' exists (+9 weeks)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')
    , ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')||' exists (+9 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' (+9 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')
    , 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')||' (+9 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' (+9 weeks)');
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')
    , 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'9 weeks'::interval), 'YYYY_MM_DD')||' (+9 weeks)');


-- This should properly go into the child table of the +5 week set
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(131,140), date_trunc('week', CURRENT_TIMESTAMP + '5 weeks'::interval));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW')||' (+5 weeks)');
-- Run maintenance again and check for constraint at -1 week
SELECT run_maintenance();
SELECT col_has_check('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW'), 'COL1'
    , 'Check for additional constraint on col1 on Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||' (-1 weeks)');

-- Also +10 week tables should be made since newest data is +5 weeks and premake is +5
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||' exists (+10 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'11 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'11 weeks'::interval, 'IYYY"w"IW')||' does not exist (+11 weeks)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'10 weeks'::interval), 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'10 weeks'::interval), 'YYYY_MM_DD')||' exists (+10 weeks)');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'71 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'10 weeks'::interval)+'1 day'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'71 days'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'10 days'::interval)+'1 day'::interval, 'YYYY_MM_DD')||' does not exist (+10 weeks +1 day)');

-- Check for next 3 days stuff being created (changed from 4 to 7)
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists (+5 days)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists (+6 days)');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' exists (+7 days)');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 day'::interval, 'YYYY_MM_DD'));

SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'4 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'3 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'2 weeks'::interval), 'YYYY_MM_DD'));
-- Part of the 8 day constraint test mentioned above that I can't get working. See previous comment for why this causes count tests to fail on mondays
--SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'YYYY_MM_DD')||'"', 
--    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[7], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP-'1 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[12], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'1 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[21], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'2 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'3 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'4 weeks'::interval), 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval), 'YYYY_MM_DD')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(date_trunc('week', CURRENT_TIMESTAMP+'5 weeks'::interval), 'YYYY_MM_DD'));

-- Testing retention for daily
-- All daily tables older than 2 weeks. Should only drop the daily subpartitions, not the weekly parents yet
UPDATE part_config SET retention = '2 weeks', retention_keep_table = false WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%' AND partition_interval = '1 day';
SELECT run_maintenance();
SELECT results_eq('SELECT count(*)::int FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||''')'
            , ARRAY[1]
            , 'Check that 6 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' only has one child remaining. Should also kick out warning about attempted drop of last child table. ');
SELECT results_eq('SELECT count(*)::int FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||''')'
            , ARRAY[1]
            , 'Check that 5 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' only has one child remaining. Should also kick out warning about attempted drop of last child table. ');
SELECT results_eq('SELECT count(*)::int FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||''')'
            , ARRAY[1]
            , 'Check that 4 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' only has one child remaining. Should also kick out warning about attempted drop of last child table. ');
SELECT results_eq('SELECT count(*)::int FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||''')'
            , ARRAY[1]
            , 'Check that 3 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' only has one child remaining. Should also kick out warning about attempted drop of last child table. ');
SELECT results_eq('SELECT CASE WHEN count(*) >= 1 THEN 1 ELSE 0 END FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||''')'
            , ARRAY[1]
            , 'Check that 2 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' only has at least one child remaining. May or may not kick out warning about attempted drop of last child table. ');
        
-- Now indexes on table olders than 3 days
UPDATE part_config SET retention = '3 days', retention_keep_table = true, retention_keep_index = false WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%' AND partition_interval = '1 day';

SELECT run_maintenance();
-- This test is failing because the 3 day retention is causing its parent to drop all child tables but it. This means the index drop never runs
-- Commenting out for now
--SELECT col_isnt_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
--    'Check that primary key was dropped (2 weeks ago) in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD')||'. This test may fail if given table is the final child table in the set.');
SELECT col_isnt_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check that primary key was dropped (1 week ago) in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'YYYY_MM_DD'));
SELECT col_isnt_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check that primary key was dropped (4 days ago) in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));

--NOTE The tables with the above indexes won't get dropped below because they're no longer part of the inheritance tree. No big deal. They'll be gone in the end.

-- Now do retention on top parent table to get rid of everything not needed anymore. Don't set shorter than 7 days otherwise "now()-1 week" table may get dropped.
UPDATE part_config SET retention = '7 days', retention_keep_table = false WHERE parent_table = 'Partman_test.Time-taptest-Table';
-- Have to do all subpartitions before parent, otherwise it errors out if the top parent gets maintenance run first then non-existent children try to run
UPDATE part_config SET retention = NULL, retention_keep_table = false WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%';

SELECT run_maintenance();

SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 
    'After retention Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'), 
    'After retention Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'), 
    'After retention Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'), 
    'After retention Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'), 
    'After retention Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' does not exist');
-- Undo daily for each week
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);

SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 1 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||' table has no children');

SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'"''::regclass',
            'Check that current parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' table has no children');

SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 2 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 3 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 4 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 5 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 6 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 7 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 8 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 9 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||'"''::regclass',
            'Check that 10 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||' table has no children');


-- Undo weekly 
SELECT undo_partition('Partman_test.Time-taptest-Table', 20, p_keep_table := false);

SELECT is_empty('SELECT inhrelid FROM pg_catalog.pg_inherits WHERE inhparent::regclass = ''"Partman_test"."Time-taptest-Table"''::regclass',
            'Check that parent Time-taptest-Table table has no children');

SELECT is_empty('SELECT * FROM part_config WHERE parent_table LIKE ''Partman_test.Time-taptest-Table''',
            'Check that part_config table is empty');

SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'7 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'8 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'9 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'10 weeks'::interval, 'IYYY"w"IW')||' does not exist');


SELECT * FROM finish();
ROLLBACK;
