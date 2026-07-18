\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

SELECT plan(1);

SELECT undo_partition('partman_reindex_test.test_reindex', 20, p_keep_table := false);
DROP SCHEMA IF EXISTS partman_reindex_test CASCADE;

SELECT pass('Cleanup Done');

SELECT * FROM finish();
