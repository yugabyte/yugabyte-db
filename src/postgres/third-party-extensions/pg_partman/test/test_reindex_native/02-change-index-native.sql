\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

DROP INDEX template_test_reindex_stuff_idx;
CREATE INDEX template_test_reindex_morestuff_idx ON template_test_reindex (morestuff);
CREATE INDEX template_test_reindex_stuff_morestuff_idx ON template_test_reindex (stuff, morestuff);
CREATE INDEX template_test_reindex_lower_stuff_idx ON template_test_reindex(lower(stuff));
ALTER TABLE test_reindex ADD new_id bigint;
ALTER TABLE template_test_reindex ADD new_id bigint;
UPDATE test_reindex SET new_id = id;
ALTER TABLE template_test_reindex DROP CONSTRAINT test_reindex_id_pkey;
ALTER TABLE template_test_reindex ADD CONSTRAINT test_reindex_new_id primary key (new_id);

SELECT plan(3);

SELECT hasnt_index('partman_reindex_test', 'template_test_reindex', 'template_test_reindex_stuff_idx', 'Check for stuff index in template_test_reindex');
SELECT has_index('partman_reindex_test', 'template_test_reindex', 'template_test_reindex_morestuff_idx', ARRAY['morestuff'], 'Check for stuff index in template_test_reindex');
SELECT col_is_pk('partman_reindex_test', 'template_test_reindex', ARRAY['new_id'], 'Check for new primary key in template_test_reindex');
SELECT diag('!!! Now run reapply_index.py on "partman_reindex_test.test_reindex" with the --primary option to apply the new indexes to all the children !!!');
SELECT diag('!!! After that completes, run 03-check-indexes.sql !!!'); 
SELECT diag('!!! You can set any options you''d like on the python script to test them. Re-run from test 01 to test different options !!! ');
SELECT * FROM finish();

