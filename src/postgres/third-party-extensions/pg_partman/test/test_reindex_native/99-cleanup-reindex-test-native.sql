\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

SELECT plan(1);

-- Have to create this table later due to columns changing during test
CREATE TABLE IF NOT EXISTS undo_test_reindex (LIKE test_reindex);

SELECT undo_partition('partman_reindex_test.test_reindex', 20, p_target_table := 'partman_reindex_test.undo_test_reindex', p_keep_table := false);
DROP SCHEMA IF EXISTS partman_reindex_test CASCADE;

SELECT pass('Cleanup Done');

SELECT * FROM finish();
