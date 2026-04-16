-- ########## NATIVE ID PARENT / TIME SUBPARENT TESTS ##########
-- Additional tests: no pg_jobmon
    -- Test using a pre-created template table and passing to create_parent. Should allow indexes to be made for initial children.
    -- Test that FK and normal index on PG11 uses parent and <=PG10 uses template
    -- Can be used to test nonsuperuser is working

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

BEGIN;
SELECT set_config('search_path','partman, public',false);

SELECT plan(324);
CREATE SCHEMA partman_test;

CREATE TABLE partman_test.fk_test_reference (col2 text unique not null);
INSERT INTO partman_test.fk_test_reference VALUES ('stuff');

-- Restore back when native supports primary key
--CREATE TABLE partman_test.id_taptest_table (col1 int primary key, col2 text, col3 timestamptz NOT NULL DEFAULT now());
CREATE TABLE partman_test.id_taptest_table (
    col1 int 
    , col2 text DEFAULT 'stuff'
    , col3 timestamptz NOT NULL DEFAULT now())
    PARTITION BY RANGE (col1);
CREATE TABLE partman_test.undo_taptest (LIKE partman_test.id_taptest_table INCLUDING ALL);
-- Template table
CREATE TABLE partman_test.template_id_taptest_table (LIKE partman_test.id_taptest_table);

-- Primary keys do not work on native in PG11 if the partition key isn't included. 
-- While col1 is the partition key on the top level, subpartition is time and it won't work there
ALTER TABLE partman_test.template_id_taptest_table ADD PRIMARY KEY (col1);

DO $pg11_objects_check$
BEGIN
IF current_setting('server_version_num')::int >= 110000 THEN
    -- Create on parent table
    CREATE INDEX ON partman_test.id_taptest_table (col3);
    ALTER TABLE partman_test.id_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);
ELSE
    -- Create on template table
    CREATE INDEX ON partman_test.template_id_taptest_table (col3);
    ALTER TABLE partman_test.template_id_taptest_table ADD FOREIGN KEY (col2) REFERENCES partman_test.fk_test_reference(col2);
END IF;
END $pg11_objects_check$;

SELECT partman.create_parent('partman_test.id_taptest_table', 'col1', 'native', '10', '{"col3"}', p_jobmon := false, p_template_table := 'partman_test.template_id_taptest_table');
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));

SELECT is_partitioned('partman_test', 'id_taptest_table', 'Check that id_taptest_table is natively partitioned');

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
SELECT col_is_fk('partman_test', 'id_taptest_table_p0', ARRAY['col2'], 'Check for foreign key in id_taptest_table_p0');
SELECT col_is_fk('partman_test', 'id_taptest_table_p10', ARRAY['col2'], 'Check for foreign key in id_taptest_table_p10');
SELECT col_is_fk('partman_test', 'id_taptest_table_p20', ARRAY['col2'], 'Check for foreign key in id_taptest_table_p20');
SELECT col_is_fk('partman_test', 'id_taptest_table_p30', ARRAY['col2'], 'Check for foreign key in id_taptest_table_p30');
SELECT col_is_fk('partman_test', 'id_taptest_table_p40', ARRAY['col2'], 'Check for foreign key in id_taptest_table_p40');
SELECT has_index('partman_test', 'id_taptest_table_p0', 'id_taptest_table_p0_col3_idx', ARRAY['col3'], 'Check for index in id_taptest_table_p0');
SELECT has_index('partman_test', 'id_taptest_table_p10', 'id_taptest_table_p10_col3_idx', ARRAY['col3'], 'Check for index key in id_taptest_table_p10');
SELECT has_index('partman_test', 'id_taptest_table_p20', 'id_taptest_table_p20_col3_idx', ARRAY['col3'], 'Check for index key in id_taptest_table_p20');
SELECT has_index('partman_test', 'id_taptest_table_p30', 'id_taptest_table_p30_col3_idx', ARRAY['col3'], 'Check for index key in id_taptest_table_p30');
SELECT has_index('partman_test', 'id_taptest_table_p40', 'id_taptest_table_p40_col3_idx', ARRAY['col3'], 'Check for index key in id_taptest_table_p40');

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from id_taptest_table_p0');

-- Create subpartition (start sub partitions 2 days before premake value)
SELECT partman.create_sub_parent('partman_test.id_taptest_table', 'col3', 'native', 'daily', p_native_check := 'yes', p_start_partition := (CURRENT_TIMESTAMP - '6 days'::interval)::text );
--Reinsert data due to child table destruction
INSERT INTO partman_test.id_taptest_table (col1) VALUES (generate_series(1,9));

SELECT is_partitioned('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 is natively partitioned');
SELECT is_partitioned('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 is natively partitioned');
SELECT is_partitioned('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 is natively partitioned');
SELECT is_partitioned('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 is natively partitioned');
SELECT is_partitioned('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 is natively partitioned');
SELECT isnt_partitioned('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 doesn''t exists as partition yet');

-- p0
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check for no data in top parent');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p0', 'Check for no data in sub-parent');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from parent table');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[9], 'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' (should be empty)');

-- p10
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check that subparent table has had data moved to partition');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p10', 'Check count from parent table _p10 (should be empty)');

-- p20
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p20', 'Check that subparent table itself _p20 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p20', 'Check count from parent table _p20 (should be empty)');

-- p30
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p30', 'Check that subparent table itself _p30 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p30', 'Check count from parent table _p30 (should be empty)');

-- p40
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 day'::interval, 'YYYY_MM_DD'));


SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p40', 'Check that subparent table itself _p40 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p40', 'Check count from parent table _p40 (should be empty)');

-- insertion round 2
INSERT INTO partman_test.id_taptest_table (col1, col3) VALUES (generate_series(10,20), CURRENT_TIMESTAMP+'1 day'::interval);
SELECT run_maintenance();
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p0', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0', ARRAY[9], 'Check count from parent table partman_test.id_taptest_table_p0');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    ARRAY[9], 'Check count from id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from parent table partman_test.id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p20', 'Check that subparent table has had data moved to partition');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[1], 'Check count from parent table partman_test.id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));


-- p50
SELECT is_partitioned('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 is natively partitioned');

SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p50', 'Check that subparent table itself _p50 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p50', 'Check count from parent table _p50 (should be empty)');

-- p60
SELECT is_partitioned('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 is natively partitioned');

SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 day'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in d_dynamic_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that parent table has had data moved to partition');
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p60', 'Check that subparent table itself _p60 is empty');
SELECT is_empty('SELECT * FROM partman_test.id_taptest_table_p60', 'Check count from parent table _p60 (should be empty)');

SELECT hasnt_table('partman_test', 'id_taptest_table_p70', 'Check id_taptest_table_p70 doesn''t exists yet');

-- make sure new data is where it should be
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table_p10', 'Check count from only parent table _p10 (should be empty)');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10', ARRAY[10], 'Check count from subparent table id_taptest_table_p10');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[10], 'Check count from id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20', ARRAY[1], 'Check count from subparent table id_taptest_table_p20');
SELECT results_eq('SELECT count(*)::int FROM partman_test.id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    ARRAY[1], 'Check count from id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'));

-- Ensure time partitioning works for all sub partitions
UPDATE part_config SET premake = 5, optimize_trigger = 5 WHERE parent_table ~ 'partman_test.id_taptest_table_p' AND partition_type = 'native';
SELECT run_maintenance();

-- Check for new time sub-partitions
-- Time data only exists for now() in _p0, so only one additional table will be created
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));


-- Time data exists for now+1 day in p10, so 2 additional tables will be created
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));

-- Time data exists for now+1 day in p20, so 2 additional tables will be created
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT has_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD')||' exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_pk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col1'], 
    'Check for primary key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'));
SELECT col_is_fk('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'), ARRAY['col2'], 
    'Check for foreign key in id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'6 days'::interval, 'YYYY_MM_DD'));

-- No time data has been inserted for p30 and higher, so no more partitions should be created for them
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');

-- Test dropping without retention set
SELECT drop_partition_time ('partman_test.id_taptest_table_p0', '2 days', p_keep_table := false);
SELECT has_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' still exists');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');


UPDATE part_config SET retention = '10', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p0';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p10';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p20';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p30';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p40';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p50';
UPDATE part_config SET retention = '2 days', retention_keep_table = false WHERE parent_table = 'partman_test.id_taptest_table_p60';

-- Test dropping with it set

SELECT run_maintenance();

SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p0_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'6 days'::interval, 'YYYY_MM_DD')||' does not exist');


SELECT undo_partition('partman_test.id_taptest_table_p10', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false); 
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');
SELECT undo_partition('partman_test.id_taptest_table_p20', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');
SELECT undo_partition('partman_test.id_taptest_table_p30', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');
SELECT undo_partition('partman_test.id_taptest_table_p40', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');
SELECT undo_partition('partman_test.id_taptest_table_p50', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');
SELECT undo_partition('partman_test.id_taptest_table_p60', 20, p_target_table := 'partman_test.undo_taptest', p_keep_table := false);
SELECT has_table('partman_test', 'template_id_taptest_table', 'Check that template table was not removed yet');


SELECT results_eq('SELECT count(*)::int FROM partman_test.undo_taptest', ARRAY[11], 'Check count from target of undo_partition');

SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP+'7 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p10_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p20_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p30_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p40_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p50_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'1 day'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'2 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'3 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'4 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP+'5 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'1 days'::interval, 'YYYY_MM_DD')||' does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD'), 
    'Check id_taptest_table_p60_p'||to_char(CURRENT_TIMESTAMP-'2 days'::interval, 'YYYY_MM_DD')||' does not exist');

SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p0''', 
    'Check that partman_test.id_taptest_table_p0 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p10''', 
    'Check that partman_test.id_taptest_table_p10 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p20''', 
    'Check that partman_test.id_taptest_table_p20 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p30''', 
    'Check that partman_test.id_taptest_table_p30 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p40''', 
    'Check that partman_test.id_taptest_table_p40 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p50''', 
    'Check that partman_test.id_taptest_table_p50 was removed from part_config');
SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table_p60''', 
    'Check that partman_test.id_taptest_table_p60 was removed from part_config');

-- Check top parent is still empty
SELECT is_empty('SELECT * FROM ONLY partman_test.id_taptest_table', 'Check that top parent table has not had any data moved to it');
-- This should return zero since above subpartitioning should have removed all data
SELECT results_eq('SELECT rows_undone::int FROM undo_partition(''partman_test.id_taptest_table'', 20, p_target_table := ''partman_test.undo_taptest'', p_keep_table := false)', ARRAY[0], 'Check that undoing top table partition returns zero rows');

SELECT hasnt_table('partman_test', 'id_taptest_table_p0', 'Check id_taptest_table_p0 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p10', 'Check id_taptest_table_p10 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p20', 'Check id_taptest_table_p20 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p30', 'Check id_taptest_table_p30 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p40', 'Check id_taptest_table_p40 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p50', 'Check id_taptest_table_p50 does not exist');
SELECT hasnt_table('partman_test', 'id_taptest_table_p60', 'Check id_taptest_table_p60 does not exist');

SELECT is_empty('SELECT parent_table from part_config where parent_table = ''partman_test.id_taptest_table''', 
    'Check that partman_test.id_taptest_table was removed from part_config');


SELECT hasnt_table('partman_test', 'template_id_taptest_table', 'Check that template table was removed');

SELECT results_eq('SELECT count(*)::int FROM ONLY partman_test.undo_taptest', ARRAY[11], 'Check count from final unpartitioned target table');

SELECT * FROM finish();
ROLLBACK;

