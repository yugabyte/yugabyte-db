\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

CREATE SCHEMA partman_reindex_test;

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

DELETE FROM part_config WHERE parent_table = 'partman_reindex_test.test_reindex';

CREATE TABLE test_reindex (id bigint, stuff text, morestuff timestamptz default now()) PARTITION BY RANGE (id);

CREATE TABLE template_test_reindex (LIKE test_reindex);
ALTER TABLE template_test_reindex ADD CONSTRAINT test_reindex_id_pkey PRIMARY KEY (id);
CREATE INDEX template_test_reindex_stuff_idx ON template_test_reindex (stuff); 
CREATE INDEX template_test_reindex_upper_stuff_idx ON template_test_reindex(upper(stuff));

SELECT plan(25);
SELECT is_partitioned('partman_reindex_test', 'test_reindex', 'Check test_reindex exists and is partition parent');
SELECT has_table('partman_reindex_test', 'template_test_reindex', 'Check template table exists');
SELECT col_is_pk('partman_reindex_test', 'template_test_reindex', ARRAY['id'], 'Check for primary key in template_test_reindex');
SELECT has_index('partman_reindex_test', 'template_test_reindex', 'template_test_reindex_stuff_idx', ARRAY['stuff'], 'Check for stuff index in template_test_reindex');
SELECT has_index('partman_reindex_test', 'template_test_reindex', 'template_test_reindex_upper_stuff_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in template_test_reindex');

SELECT create_parent('partman_reindex_test.test_reindex', 'id', 'native', '100', p_template_table := 'partman_reindex_test.template_test_reindex');

INSERT INTO test_reindex VALUES (generate_series(1,499), 'stuff'||generate_series(1, 499));
-- Create a few more child tables to test now that there's data
SELECT run_maintenance('partman_reindex_test.test_reindex');


SELECT has_table('partman_reindex_test', 'test_reindex_p0', 'Check test_reindex_p0 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p0', ARRAY['id'], 'Check for primary key in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p0');

SELECT has_table('partman_reindex_test', 'test_reindex_p100', 'Check test_reindex_p100 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p100', ARRAY['id'], 'Check for primary key in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p100');

SELECT has_table('partman_reindex_test', 'test_reindex_p200', 'Check test_reindex_p200 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p200', ARRAY['id'], 'Check for primary key in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p200');

SELECT has_table('partman_reindex_test', 'test_reindex_p300', 'Check test_reindex_p300 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p300', ARRAY['id'], 'Check for primary key in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p300');

SELECT has_table('partman_reindex_test', 'test_reindex_p400', 'Check test_reindex_p400 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p400', ARRAY['id'], 'Check for primary key in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p400');
/*
SELECT has_table('partman_reindex_test', 'test_reindex_p1100', 'Check test_reindex_p1100 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1100', ARRAY['id'], 'Check for primary key in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p1100');

SELECT has_table('partman_reindex_test', 'test_reindex_p1200', 'Check test_reindex_p1200 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1200', ARRAY['id'], 'Check for primary key in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p1200');

SELECT has_table('partman_reindex_test', 'test_reindex_p1300', 'Check test_reindex_p1300 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1300', ARRAY['id'], 'Check for primary key in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p1300');

SELECT has_table('partman_reindex_test', 'test_reindex_p1400', 'Check test_reindex_p1400 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1400', ARRAY['id'], 'Check for primary key in test_reindex_p1400');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p1400');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p1400');
SELECT partition_data_id('partman_reindex_test.test_reindex', p_batch_count := 20);
*/

SELECT diag('!!! Next run 02-change-index-native.sql !!!');

SELECT * FROM finish();


