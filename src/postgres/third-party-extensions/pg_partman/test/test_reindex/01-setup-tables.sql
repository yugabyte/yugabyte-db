\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

CREATE SCHEMA partman_reindex_test;

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

DELETE FROM part_config WHERE parent_table = 'partman_reindex_test.test_reindex';

CREATE TABLE test_reindex (id bigint, stuff text, morestuff timestamptz default now());
ALTER TABLE test_reindex ADD CONSTRAINT test_reindex_id_pkey PRIMARY KEY (id);
INSERT INTO test_reindex VALUES (generate_series(1,1000), 'stuff'||generate_series(1, 1000));
CREATE INDEX test_reindex_stuff_idx ON test_reindex (stuff); 
CREATE INDEX test_reindex_upper_stuff_idx ON test_reindex(upper(stuff));

SELECT create_parent('partman_reindex_test.test_reindex', 'id', 'partman', '100');

SELECT plan(40);
SELECT has_table('partman_reindex_test', 'test_reindex', 'Check test_reindex exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex', ARRAY['id'], 'Check for primary key in test_reindex');
SELECT has_index('partman_reindex_test', 'test_reindex', 'test_reindex_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex');
SELECT has_index('partman_reindex_test', 'test_reindex', 'test_reindex_upper_stuff_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex');

SELECT has_table('partman_reindex_test', 'test_reindex_p600', 'Check test_reindex_p600 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p600', ARRAY['id'], 'Check for primary key in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p600');

SELECT has_table('partman_reindex_test', 'test_reindex_p700', 'Check test_reindex_p700 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p700', ARRAY['id'], 'Check for primary key in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p700');

SELECT has_table('partman_reindex_test', 'test_reindex_p800', 'Check test_reindex_p800 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p800', ARRAY['id'], 'Check for primary key in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p800');

SELECT has_table('partman_reindex_test', 'test_reindex_p900', 'Check test_reindex_p900 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p900', ARRAY['id'], 'Check for primary key in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p900');

SELECT has_table('partman_reindex_test', 'test_reindex_p1000', 'Check test_reindex_p1000 exists');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1000', ARRAY['id'], 'Check for primary key in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_stuff_idx', ARRAY['stuff'], 'Check for stuff index in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p1000');

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

SELECT diag('!!! Next run 02-change-index.sql !!!');

SELECT * FROM finish();


