-- ########## TIME WEEKLY-DAILY SUBPARTITION TESTS ##########
-- Other tests: Mixed case & special characters, extra constraints, hybrid trigger puts data outside premake into proper tables, dropping only indexes in retention, grants/revokes
--NOTE Some tests failing on mondays. Thanks Garfield. Will look into further at some point.

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(181);
CREATE SCHEMA "Partman_test";
CREATE ROLE "partman-basic";
CREATE ROLE "Partman_Revoke";
CREATE ROLE partman_owner;

CREATE TABLE "Partman_test"."Time-taptest-Table" ("COL1" int primary key, col2 text, "Col-3" timestamptz NOT NULL DEFAULT now());
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(1,10), CURRENT_TIMESTAMP);
GRANT SELECT,INSERT,UPDATE ON "Partman_test"."Time-taptest-Table" TO "partman-basic";
GRANT ALL ON "Partman_test"."Time-taptest-Table" TO "Partman_Revoke";

SELECT create_parent('Partman_test.Time-taptest-Table', 'Col-3', 'time', 'weekly', '{"COL1"}', p_premake := 4, p_start_partition := to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'YYYY-MM-DD HH24:MI:SS'));
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
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(11,20), CURRENT_TIMESTAMP - '4 week'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(21,25), CURRENT_TIMESTAMP - '3 week'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(26,30), CURRENT_TIMESTAMP - '2 weeks'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(31,37), CURRENT_TIMESTAMP - '1 week'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(38,49), CURRENT_TIMESTAMP + '1 week'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(50,70), CURRENT_TIMESTAMP + '2 weeks'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(71,85), CURRENT_TIMESTAMP + '3 weeks'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(86,100), CURRENT_TIMESTAMP + '4 weeks'::interval);
-- Test hybrid trigger puts data in tables outside premake range
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(101,110), CURRENT_TIMESTAMP - '5 weeks'::interval);
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(111,130), CURRENT_TIMESTAMP - '6 weeks'::interval);
-- Add data for checking that daily subpartition constraint management is working (8 days is before new premake value below). 
-- This isn't currently working, but leaving this here for future testing if I can get this working
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(131,140), CURRENT_TIMESTAMP - '8 days'::interval);

SELECT is_empty('SELECT * FROM ONLY "Partman_test"."Time-taptest-Table"', 'Check that parent table has had no data inserted to it');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' (this week)');
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
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[17], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||' (-1 week)');
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

SELECT create_sub_parent('Partman_test.Time-taptest-Table', 'Col-3', 'time', 'daily', '{"COL1"}', p_premake := 4);

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
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')
    , 'partman-basic', ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));

SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'],
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 day'::interval, 'YYYY_MM_DD'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD')
    , 'Partman_Revoke', ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 day'::interval, 'YYYY_MM_DD'));
 
UPDATE part_config SET premake = 5 WHERE parent_table = 'Partman_test.Time-taptest-Table';
UPDATE part_config SET premake = 7 WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%';

SELECT run_maintenance();
INSERT INTO "Partman_test"."Time-taptest-Table" ("COL1", "Col-3") VALUES (generate_series(141,150), CURRENT_TIMESTAMP + '5 weeks'::interval);

-- Have to do the partitioning after the change, otherwise it can create child tables that prevent run_maintenance() from properly catching up
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10] , 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[20], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[5], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[5], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[17], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 week'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[12], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 week'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[21], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[15], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[15], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT partition_data_time(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||''', 10)::int'
    , ARRAY[10], 'Check that partitioning function returns correct count of rows moved for Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));

-- Should automatically put additional constraint on -6 weeks ago
SELECT col_has_check('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'), 'COL1'
    , 'Check for additional constraint on col1 on Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW'));

-- Check for older sub-partitions having their child tables created due to data getting partitioned out
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'YYYY_MM_DD')||' exists');

-- Check for next week stuff being created (changed from 4 to 5)
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' exists');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW'), 
    'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 weeks'::interval, 'IYYY"w"IW')||' does not exist');
SELECT col_is_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), ARRAY['COL1'], 
    'Check for primary key in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'partman-basic', 
    ARRAY['SELECT','INSERT','UPDATE'], 
    'Check partman-basic privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT table_privs_are('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 'Partman_Revoke', 
    ARRAY['SELECT'], 
    'Check Partman_Revoke privileges of Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 week'::interval, 'IYYY"w"IW'));

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

-- Check for next 3 days stuff being created (changed from 4 to 7)
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')
        , 'Check Time-taptest-Table_'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'8 days'::interval, 'YYYY_MM_DD')||' does not exist');
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

SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[5], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'8 days'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[7], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[12], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[21], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[15], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'YYYY_MM_DD')||'"', 
    ARRAY[10], 'Check count from Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'YYYY_MM_DD'));

-- Testing retention for daily
-- All daily tables older than 2 weeks. Should only drop the daily subpartitions, not the weekly parents yet
UPDATE part_config SET retention = '2 weeks', retention_keep_table = false WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%' AND partition_interval = '1 day';
SELECT run_maintenance();
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 6 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'6 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 5 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'5 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 4 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'4 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 3 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'3 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT results_eq('SELECT CASE WHEN count(*) >= 1 THEN 1 ELSE 0 END FROM partman.show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||''')',
            ARRAY[1], 'Check that 2 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||' still has at least 1 child');
-- Now indexes on table olders than 3 days
UPDATE part_config SET retention = '3 days', retention_keep_table = true, retention_keep_index = false WHERE parent_table LIKE 'Partman_test.Time-taptest-Table_p%' AND partition_interval = '1 day';
SELECT run_maintenance();
SELECT col_isnt_pk('Partman_test', 'Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD'), ARRAY['COL1'], 
    'Check that primary key was dropped (2 weeks ago) in Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'IYYY"w"IW')||'_p'||to_char(CURRENT_TIMESTAMP-'2 weeks'::interval, 'YYYY_MM_DD'));
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
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);
SELECT undo_partition_time('Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW'), 20, p_keep_table := false);

SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week old parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP-'1 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||''')',
            'Check that current parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'1 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'2 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'3 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'4 weeks'::interval, 'IYYY"w"IW')||' table has no children');
SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||''')',
            'Check that 1 week future parent Time-taptest-Table_p'||to_char(CURRENT_TIMESTAMP+'5 weeks'::interval, 'IYYY"w"IW')||' table has no children');

-- Undo weekly 
SELECT undo_partition_time('Partman_test.Time-taptest-Table', 20, p_keep_table := false);

SELECT is_empty('SELECT partition_tablename FROM show_partitions(''Partman_test.Time-taptest-Table'')',
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

SELECT * FROM finish();
ROLLBACK;
