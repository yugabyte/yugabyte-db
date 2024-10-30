-- ########## ID MIXED CASE SUBPARTITION TESTS ##########
-- Other tests: Foreign keys, hybrid trigger puts data outside premake into proper tables, retention schema, no jobmon, test 50% trigger with mixed case
\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(374);
CREATE SCHEMA "Partman_test";
CREATE SCHEMA "Partman-retention-Test";

CREATE TABLE "Partman_test"."FK_test_reference" (col2 text not null, col4 text not null);
CREATE UNIQUE INDEX ON "Partman_test"."FK_test_reference"(col2, col4);
INSERT INTO "Partman_test"."FK_test_reference" VALUES ('stuff', 'stuff');

CREATE TABLE "Partman_test"."ID-taptest_Table" (
    "COL1" int primary key
    , col2 text default 'stuff'
    , "Col-3" timestamptz NOT NULL DEFAULT now()
    , col4 text default 'stuff'
    , FOREIGN KEY (col2, col4) REFERENCES "Partman_test"."FK_test_reference"(col2, col4) MATCH FULL ON DELETE RESTRICT DEFERRABLE);
INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(1,50000));

SELECT create_parent('Partman_test.ID-taptest_Table', 'COL1', 'partman', '10000', p_jobmon := false, p_premake := 2);
UPDATE part_config SET drop_cascade_fk = TRUE WHERE parent_table = 'Partman_test.ID-taptest_Table';

SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table'', p_batch_count := 20)::int', ARRAY[50000], 'Check that partitioning function returns correct count of rows moved');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table"', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table"', ARRAY[50000], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0"', ARRAY[9999], 'Check count from ID-taptest_Table_p0');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000"', ARRAY[10000], 'Check count from ID-taptest_Table_p10000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000"', ARRAY[10000], 'Check count from ID-taptest_Table_p20000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000"', ARRAY[10000], 'Check count from ID-taptest_Table_p30000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000"', ARRAY[10000], 'Check count from ID-taptest_Table_p40000');

SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p10000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p10000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p20000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p20000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p30000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p30000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p40000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p40000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p50000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p50000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p60000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p60000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p70000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p70000');

SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p0', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p0');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p20000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p20000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p30000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p30000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p40000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p40000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p50000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p50000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p60000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p60000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p70000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p70000');


INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(50001, 70000));
SELECT run_maintenance();
INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(70001, 90000));
SELECT run_maintenance();
INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(90001, 100000));

SELECT has_table('Partman_test', 'ID-taptest_Table_p80000', 'Check "ID-taptest_Table_p80000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p90000', 'Check "ID-taptest_Table_p90000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000', 'Check "ID-taptest_Table_p100000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p110000', 'Check "ID-taptest_Table_p110000 exists');
-- Won't get created by 50% rule unless 100000 row table gets half full. run_maintenance() call below after subpart will create it
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p120000', 'Check "ID-taptest_Table_p120000 does not exist');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000"', ARRAY[10000], 'Check count from ID-taptest_Table_p50000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000"', ARRAY[10000], 'Check count from ID-taptest_Table_p60000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000"', ARRAY[10000], 'Check count from ID-taptest_Table_p70000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000"', ARRAY[10000], 'Check count from ID-taptest_Table_p80000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000"', ARRAY[10000], 'Check count from ID-taptest_Table_p90000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p100000"', ARRAY[1], 'Check count from ID-taptest_Table_p100000');

SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p80000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p80000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p110000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p110000');

SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p80000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p80000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p90000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p90000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p110000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p110000');

SELECT create_sub_parent('Partman_test.ID-taptest_Table', 'COL1', 'partman', '1000', p_jobmon := false, p_premake := 2);
-- Test for normal partitions that should be made based on current max value of 100,000
SELECT has_table('Partman_test', 'ID-taptest_Table_p90000_p98000', 'Check ID-taptest_Table_p90000_p98000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p90000_p99000', 'Check ID-taptest_Table_p90000_p99000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000_p100000', 'Check ID-taptest_Table_p100000_p100000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000_p101000', 'Check ID-taptest_Table_p100000_p101000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000_p102000', 'Check ID-taptest_Table_p100000_p102000 exists');

-- Tests that ensure minimal partition was made in all other sets
SELECT has_table('Partman_test', 'ID-taptest_Table_p0_p0', 'Check ID-taptest_Table_p0_p0 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p10000_p10000', 'Check ID-taptest_Table_p10000_p10000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p20000_p20000', 'Check ID-taptest_Table_p20000_p20000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p30000_p30000', 'Check ID-taptest_Table_p30000_p30000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p40000_p40000', 'Check ID-taptest_Table_p40000_p40000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p50000_p50000', 'Check ID-taptest_Table_p50000_p50000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p60000_p60000', 'Check ID-taptest_Table_p60000_p60000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p70000_p70000', 'Check ID-taptest_Table_p70000_p70000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p80000_p80000', 'Check ID-taptest_Table_p80000_p80000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p110000_p110000', 'Check ID-taptest_Table_p110000_p110000 exists');

-- Partition all data again
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p0'', p_batch_count := 20)::int', ARRAY[9999], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p0');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p10000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p10000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p20000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p20000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p30000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p30000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p40000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p40000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p50000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p50000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p60000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p60000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p70000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p70000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p80000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p80000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p90000'', p_batch_count := 20)::int', ARRAY[10000], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p90000');
SELECT results_eq('SELECT partition_data_id(''Partman_test.ID-taptest_Table_p100000'', p_batch_count := 20)::int', ARRAY[1], 'Check that partitioning function returns correct count of rows moved for ID-taptest_Table_p100000');
-- Test that all partitions have their data/exist
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table"', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table"', ARRAY[100000], 'Check count from parent table');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p0"', 'Check that parent table ID-taptest_Table_p0 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p0"', ARRAY[999], 'Check count from ID-taptest_Table_p0_p0');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p1000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p1000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p2000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p2000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p3000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p3000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p4000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p4000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p5000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p5000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p6000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p6000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p7000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p7000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p8000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p8000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p0_p9000"', ARRAY[1000], 'Check count from ID-taptest_Table_p0_p9000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p10000"', 'Check that parent table ID-taptest_Table_p10000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p10000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p10000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p11000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p11000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p12000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p12000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p13000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p13000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p14000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p14000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p15000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p15000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p16000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p16000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p17000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p17000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p18000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p18000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p10000_p19000"', ARRAY[1000], 'Check count from ID-taptest_Table_p10000_p19000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p20000"', 'Check that parent table ID-taptest_Table_p20000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p20000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p20000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p21000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p21000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p22000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p22000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p23000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p23000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p24000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p24000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p25000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p25000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p26000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p26000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p27000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p27000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p28000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p28000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p20000_p29000"', ARRAY[1000], 'Check count from ID-taptest_Table_p20000_p29000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p30000"', 'Check that parent table ID-taptest_Table_p30000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p30000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p30000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p31000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p31000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p32000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p32000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p33000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p33000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p34000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p34000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p35000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p35000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p36000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p36000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p37000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p37000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p38000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p38000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p30000_p39000"', ARRAY[1000], 'Check count from ID-taptest_Table_p30000_p39000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p40000"', 'Check that parent table ID-taptest_Table_p40000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p40000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p40000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p41000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p41000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p42000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p42000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p43000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p43000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p44000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p44000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p45000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p45000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p46000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p46000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p47000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p47000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p48000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p48000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p49000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p49000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p50000"', 'Check that parent table ID-taptest_Table_p50000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p50000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p50000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p51000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p51000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p52000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p52000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p53000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p53000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p54000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p54000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p55000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p55000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p56000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p56000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p57000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p57000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p58000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p58000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p50000_p59000"', ARRAY[1000], 'Check count from ID-taptest_Table_p50000_p59000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p60000"', 'Check that parent table ID-taptest_Table_p60000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p60000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p60000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p61000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p61000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p62000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p62000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p63000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p63000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p64000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p64000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p65000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p65000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p66000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p66000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p67000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p67000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p68000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p68000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p60000_p69000"', ARRAY[1000], 'Check count from ID-taptest_Table_p60000_p69000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p70000"', 'Check that parent table ID-taptest_Table_p70000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p70000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p70000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p71000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p71000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p72000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p72000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p73000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p73000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p74000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p74000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p75000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p75000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p76000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p76000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p77000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p77000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p78000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p78000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p70000_p79000"', ARRAY[1000], 'Check count from ID-taptest_Table_p70000_p79000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p80000"', 'Check that parent table ID-taptest_Table_p80000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p80000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p80000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p81000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p81000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p82000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p82000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p83000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p83000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p84000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p84000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p85000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p85000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p86000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p86000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p87000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p87000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p88000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p88000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p80000_p89000"', ARRAY[1000], 'Check count from ID-taptest_Table_p80000_p89000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p90000"', 'Check that parent table ID-taptest_Table_p90000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p90000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p90000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p91000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p91000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p92000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p92000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p93000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p93000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p94000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p94000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p95000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p95000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p96000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p96000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p97000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p97000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p98000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p98000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p90000_p99000"', ARRAY[1000], 'Check count from ID-taptest_Table_p90000_p99000');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p100000"', 'Check that parent table ID-taptest_Table_p100000 has had data moved to partition ');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p100000_p100000"', ARRAY[1], 'Check count from ID-taptest_Table_p100000_p100000');

SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p0', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p0');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p1000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p1000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p2000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p2000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p3000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p3000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p4000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p4000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p5000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p5000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p6000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p6000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p7000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p7000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p8000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p8000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p0_p9000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p0_p9000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p90000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p90000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p91000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p91000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p92000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p92000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p93000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p93000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p94000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p94000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p95000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p95000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p96000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p96000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p97000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p97000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p98000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p98000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p90000_p99000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p90000_p99000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000_p100000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000_p100000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000_p101000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000_p101000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000_p102000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000_p102000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p110000_p110000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p110000_p110000');

SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p10000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_10000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p11000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_11000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p12000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_12000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p13000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_13000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p14000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_14000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p15000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_15000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p16000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_16000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p17000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_17000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p18000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_18000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p10000_p19000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p10000_19000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000_p100000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000_100000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000_p101000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000_101000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000_p102000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000_102000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p110000_p110000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p110000_110000');

-- Remove & Replace some rows to test hybrid trigger dynamic fallback
DELETE FROM "Partman_test"."ID-taptest_Table_p40000";
SELECT is_empty('SELECT * FROM "Partman_test"."ID-taptest_Table_p40000"', 'Make sure test table with deleted rows is empty');
SELECT is_empty('SELECT * FROM "Partman_test"."ID-taptest_Table_p40000_p40000"', 'Make sure test table with deleted rows is empty');
INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(40000,49999));
-- Check that all is back as it was
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table"', 'Check that parent table top is empty');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table"', ARRAY[100000], 'Check count from parent table');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p40000"', 'Check that parent table ID-taptest_Table_p40000 is empty');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p40000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p40000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p41000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p41000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p42000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p42000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p43000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p43000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p44000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p44000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p45000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p45000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p46000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p46000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p47000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p47000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p48000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p48000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p40000_p49000"', ARRAY[1000], 'Check count from ID-taptest_Table_p40000_p49000');


-- Check that static part of  trigger is working
INSERT INTO "Partman_test"."ID-taptest_Table" ("COL1") VALUES (generate_series(100001,102000));
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table"', ARRAY[102000], 'Check count from parent table');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table"', 'Check that parent table top is empty');
SELECT is_empty('SELECT * FROM ONLY "Partman_test"."ID-taptest_Table_p100000"', 'Check that parent table ID-taptest_Table_p100000 is empty');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p100000_p100000"', ARRAY[1000], 'Check count from ID-taptest_Table_p100000_p100000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p100000_p101000"', ARRAY[1000], 'Check count from ID-taptest_Table_p100000_p101000');
SELECT results_eq('SELECT count(*)::int FROM "Partman_test"."ID-taptest_Table_p100000_p102000"', ARRAY[1], 'Check count from ID-taptest_Table_p100000_p102000');

SELECT run_maintenance();
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000_p103000', 'Check ID-taptest_Table_p100000_p103000 exists');
SELECT has_table('Partman_test', 'ID-taptest_Table_p100000_p104000', 'Check ID-taptest_Table_p100000_p104000 exists');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p105000', 'Check ID-taptest_Table_p100000_p105000 does not exist');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000_p103000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000_p103000');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p100000_p104000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p100000_p104000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000_p103000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000_p103000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p100000_p104000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p100000_p104000');

SELECT has_table('Partman_test', 'ID-taptest_Table_p120000', 'Check "ID-taptest_Table_p120000 exists');
SELECT col_is_pk('Partman_test', 'ID-taptest_Table_p120000', ARRAY['COL1'], 'Check for primary key in ID-taptest_Table_p120000');
SELECT col_is_fk('Partman_test', 'ID-taptest_Table_p120000', ARRAY['col2', 'col4'], 'Check for inherited foreign key in ID-taptest_Table_p120000');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p130000', 'Check "ID-taptest_Table_p130000 does not exist');

-- Testing retention for 50000 on top parent 
-- Retention for the sub-partitions won't work due to the nature of how it's done (current partition max - retention value). The max in each partition has no bearing on the overall integer value unlike time subpartitioning
UPDATE part_config SET retention = '50000', retention_keep_table = false WHERE parent_table = 'Partman_test.ID-taptest_Table';
SELECT run_maintenance();

SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0', 'Check ID-taptest_Table_p0 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p0', 'Check ID-taptest_Table_p0_p0 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p1000', 'Check ID-taptest_Table_p0_p1000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p2000', 'Check ID-taptest_Table_p0_p2000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p3000', 'Check ID-taptest_Table_p0_p3000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p4000', 'Check ID-taptest_Table_p0_p4000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p5000', 'Check ID-taptest_Table_p0_p5000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p6000', 'Check ID-taptest_Table_p0_p6000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p7000', 'Check ID-taptest_Table_p0_p7000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p8000', 'Check ID-taptest_Table_p0_p8000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p0_p9000', 'Check ID-taptest_Table_p0_p9000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000', 'Check ID-taptest_Table_p10000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p10000', 'Check ID-taptest_Table_p10000_p10000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p11000', 'Check ID-taptest_Table_p10000_p11000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p12000', 'Check ID-taptest_Table_p10000_p12000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p13000', 'Check ID-taptest_Table_p10000_p13000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p14000', 'Check ID-taptest_Table_p10000_p14000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p15000', 'Check ID-taptest_Table_p10000_p15000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p16000', 'Check ID-taptest_Table_p10000_p16000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p17000', 'Check ID-taptest_Table_p10000_p17000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p18000', 'Check ID-taptest_Table_p10000_p18000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p10000_p19000', 'Check ID-taptest_Table_p10000_p19000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000', 'Check ID-taptest_Table_p20000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p20000', 'Check ID-taptest_Table_p20000_p20000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p21000', 'Check ID-taptest_Table_p20000_p21000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p22000', 'Check ID-taptest_Table_p20000_p22000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p23000', 'Check ID-taptest_Table_p20000_p23000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p24000', 'Check ID-taptest_Table_p20000_p24000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p25000', 'Check ID-taptest_Table_p20000_p25000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p26000', 'Check ID-taptest_Table_p20000_p26000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p27000', 'Check ID-taptest_Table_p20000_p27000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p28000', 'Check ID-taptest_Table_p20000_p28000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p20000_p29000', 'Check ID-taptest_Table_p20000_p29000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000', 'Check ID-taptest_Table_p30000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p30000', 'Check ID-taptest_Table_p30000_p30000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p31000', 'Check ID-taptest_Table_p30000_p31000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p32000', 'Check ID-taptest_Table_p30000_p32000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p33000', 'Check ID-taptest_Table_p30000_p33000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p34000', 'Check ID-taptest_Table_p30000_p34000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p35000', 'Check ID-taptest_Table_p30000_p35000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p36000', 'Check ID-taptest_Table_p30000_p36000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p37000', 'Check ID-taptest_Table_p30000_p37000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p38000', 'Check ID-taptest_Table_p30000_p38000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p30000_p39000', 'Check ID-taptest_Table_p30000_p39000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000', 'Check ID-taptest_Table_p40000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p40000', 'Check ID-taptest_Table_p40000_p40000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p41000', 'Check ID-taptest_Table_p40000_p41000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p42000', 'Check ID-taptest_Table_p40000_p42000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p43000', 'Check ID-taptest_Table_p40000_p43000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p44000', 'Check ID-taptest_Table_p40000_p44000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p45000', 'Check ID-taptest_Table_p40000_p45000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p46000', 'Check ID-taptest_Table_p40000_p46000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p47000', 'Check ID-taptest_Table_p40000_p47000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p48000', 'Check ID-taptest_Table_p40000_p48000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p40000_p49000', 'Check ID-taptest_Table_p40000_p49000 does not exist');
-- Make sure 50000 are still here
SELECT has_table('Partman_test', 'ID-taptest_Table_p50000', 'Check ID-taptest_Table_p50000 still exists after maintenance');
SELECT has_table('Partman_test', 'ID-taptest_Table_p50000_p50000', 'Check ID-taptest_Table_p50000_p50000 still exists after maintenance');

SELECT undo_partition('Partman_test.ID-taptest_Table_p50000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p60000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p70000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p80000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p90000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p100000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p110000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table_p120000', 20, p_keep_table := false);
SELECT undo_partition('Partman_test.ID-taptest_Table', 20, p_keep_table := false);

SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000', 'Check ID-taptest_Table_p50000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p50000', 'Check ID-taptest_Table_p50000_p50000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p51000', 'Check ID-taptest_Table_p50000_p51000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p52000', 'Check ID-taptest_Table_p50000_p52000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p53000', 'Check ID-taptest_Table_p50000_p53000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p54000', 'Check ID-taptest_Table_p50000_p44000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p55000', 'Check ID-taptest_Table_p50000_p55000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p56000', 'Check ID-taptest_Table_p50000_p56000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p57000', 'Check ID-taptest_Table_p50000_p57000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p58000', 'Check ID-taptest_Table_p50000_p58000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p50000_p59000', 'Check ID-taptest_Table_p50000_p59000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000', 'Check ID-taptest_Table_p60000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p60000', 'Check ID-taptest_Table_p60000_p60000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p61000', 'Check ID-taptest_Table_p60000_p61000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p62000', 'Check ID-taptest_Table_p60000_p62000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p63000', 'Check ID-taptest_Table_p60000_p63000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p64000', 'Check ID-taptest_Table_p60000_p64000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p65000', 'Check ID-taptest_Table_p60000_p65000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p66000', 'Check ID-taptest_Table_p60000_p66000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p67000', 'Check ID-taptest_Table_p60000_p67000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p68000', 'Check ID-taptest_Table_p60000_p68000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p60000_p69000', 'Check ID-taptest_Table_p60000_p69000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000', 'Check ID-taptest_Table_p70000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p70000', 'Check ID-taptest_Table_p70000_p70000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p71000', 'Check ID-taptest_Table_p70000_p71000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p72000', 'Check ID-taptest_Table_p70000_p72000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p73000', 'Check ID-taptest_Table_p70000_p73000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p74000', 'Check ID-taptest_Table_p70000_p74000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p75000', 'Check ID-taptest_Table_p70000_p75000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p76000', 'Check ID-taptest_Table_p70000_p76000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p77000', 'Check ID-taptest_Table_p70000_p77000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p78000', 'Check ID-taptest_Table_p70000_p78000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p70000_p79000', 'Check ID-taptest_Table_p70000_p79000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000', 'Check ID-taptest_Table_p80000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p80000', 'Check ID-taptest_Table_p80000_p80000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p81000', 'Check ID-taptest_Table_p80000_p81000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p82000', 'Check ID-taptest_Table_p80000_p82000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p83000', 'Check ID-taptest_Table_p80000_p83000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p84000', 'Check ID-taptest_Table_p80000_p84000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p85000', 'Check ID-taptest_Table_p80000_p85000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p86000', 'Check ID-taptest_Table_p80000_p86000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p87000', 'Check ID-taptest_Table_p80000_p87000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p88000', 'Check ID-taptest_Table_p80000_p88000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p80000_p89000', 'Check ID-taptest_Table_p80000_p89000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000', 'Check ID-taptest_Table_p90000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p90000', 'Check ID-taptest_Table_p90000_p90000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p91000', 'Check ID-taptest_Table_p90000_p91000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p92000', 'Check ID-taptest_Table_p90000_p92000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p93000', 'Check ID-taptest_Table_p90000_p93000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p94000', 'Check ID-taptest_Table_p90000_p94000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p95000', 'Check ID-taptest_Table_p90000_p95000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p96000', 'Check ID-taptest_Table_p90000_p96000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p97000', 'Check ID-taptest_Table_p90000_p97000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p98000', 'Check ID-taptest_Table_p90000_p98000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p90000_p99000', 'Check ID-taptest_Table_p90000_p99000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000', 'Check ID-taptest_Table_p100000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p100000', 'Check ID-taptest_Table_p100000_p100000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p101000', 'Check ID-taptest_Table_p100000_p101000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p102000', 'Check ID-taptest_Table_p100000_p102000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p103000', 'Check ID-taptest_Table_p100000_p103000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p100000_p104000', 'Check ID-taptest_Table_p100000_p104000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p110000', 'Check ID-taptest_Table_p110000 does not exist');
SELECT hasnt_table('Partman_test', 'ID-taptest_Table_p120000', 'Check ID-taptest_Table_p120000 does not exist');

SELECT is_empty('SELECT * FROM part_config WHERE parent_table LIKE ''Partman_test.ID-taptest_Table''', 'Check that part_config table is empty');

SELECT * FROM finish();

ROLLBACK;

