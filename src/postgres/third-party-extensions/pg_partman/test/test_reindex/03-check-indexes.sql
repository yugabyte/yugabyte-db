\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman_reindex_test, partman, public',false);

SELECT plan(90);

SELECT hasnt_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_stuff_idx', 'Check stuff index was removed in test_reindex_p0');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_stuff_idx', 'Check stuff index was removed in test_reindex_p100');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_stuff_idx', 'Check stuff index was removed in test_reindex_p200');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_stuff_idx', 'Check stuff index was removed in test_reindex_p300');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_stuff_idx', 'Check stuff index was removed in test_reindex_p400');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p500', 'test_reindex_p500_stuff_idx', 'Check stuff index was removed in test_reindex_p500');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_stuff_idx', 'Check stuff index was removed in test_reindex_p600');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_stuff_idx', 'Check stuff index was removed in test_reindex_p700');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_stuff_idx', 'Check stuff index was removed in test_reindex_p800');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_stuff_idx', 'Check stuff index was removed in test_reindex_p900');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_stuff_idx', 'Check stuff index was removed in test_reindex_p1000');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_stuff_idx', 'Check stuff index was removed in test_reindex_p1100');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_stuff_idx', 'Check stuff index was removed in test_reindex_p1200');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_stuff_idx', 'Check stuff index was removed in test_reindex_p1300');
SELECT hasnt_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_stuff_idx', 'Check stuff index was removed in test_reindex_p1400');

SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p500', 'test_reindex_p500_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p500');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_morestuff_idx', ARRAY['morestuff'], 'Check for morestuff index in test_reindex_p1400');

SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for morestuff index in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p500', 'test_reindex_p500_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p500');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_stuff_morestuff_idx', ARRAY['stuff', 'morestuff'], 'Check for stuff,morestuff index in test_reindex_p1400');

SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) index in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p500', 'test_reindex_p500_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p500');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_idx', ARRAY['lower(stuff)'], 'Check for lower(stuff) stuff index in test_reindex_p1400');

SELECT has_index('partman_reindex_test', 'test_reindex_p0', 'test_reindex_p0_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) index in test_reindex_p0');
SELECT has_index('partman_reindex_test', 'test_reindex_p100', 'test_reindex_p100_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p100');
SELECT has_index('partman_reindex_test', 'test_reindex_p200', 'test_reindex_p200_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p200');
SELECT has_index('partman_reindex_test', 'test_reindex_p300', 'test_reindex_p300_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p300');
SELECT has_index('partman_reindex_test', 'test_reindex_p400', 'test_reindex_p400_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p400');
SELECT has_index('partman_reindex_test', 'test_reindex_p500', 'test_reindex_p500_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p500');
SELECT has_index('partman_reindex_test', 'test_reindex_p600', 'test_reindex_p600_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p600');
SELECT has_index('partman_reindex_test', 'test_reindex_p700', 'test_reindex_p700_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p700');
SELECT has_index('partman_reindex_test', 'test_reindex_p800', 'test_reindex_p800_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p800');
SELECT has_index('partman_reindex_test', 'test_reindex_p900', 'test_reindex_p900_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p900');
SELECT has_index('partman_reindex_test', 'test_reindex_p1000', 'test_reindex_p1000_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p1000');
SELECT has_index('partman_reindex_test', 'test_reindex_p1100', 'test_reindex_p1100_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p1100');
SELECT has_index('partman_reindex_test', 'test_reindex_p1200', 'test_reindex_p1200_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p1200');
SELECT has_index('partman_reindex_test', 'test_reindex_p1300', 'test_reindex_p1300_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p1300');
SELECT has_index('partman_reindex_test', 'test_reindex_p1400', 'test_reindex_p1400_upper_idx', ARRAY['upper(stuff)'], 'Check for upper(stuff) stuff index in test_reindex_p1400');

SELECT col_is_pk('partman_reindex_test', 'test_reindex_p0', ARRAY['new_id'], 'Check for new primary key in test_reindex_p0');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p100', ARRAY['new_id'], 'Check for new primary key in test_reindex_p100');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p200', ARRAY['new_id'], 'Check for new primary key in test_reindex_p200');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p300', ARRAY['new_id'], 'Check for new primary key in test_reindex_p300');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p400', ARRAY['new_id'], 'Check for new primary key in test_reindex_p400');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p500', ARRAY['new_id'], 'Check for new primary key in test_reindex_p500');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p600', ARRAY['new_id'], 'Check for new primary key in test_reindex_p600');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p700', ARRAY['new_id'], 'Check for new primary key in test_reindex_p700');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p800', ARRAY['new_id'], 'Check for new primary key in test_reindex_p800');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p900', ARRAY['new_id'], 'Check for new primary key in test_reindex_p900');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1000', ARRAY['new_id'], 'Check for new primary key in test_reindex_p1000');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1100', ARRAY['new_id'], 'Check for new primary key in test_reindex_p1100');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1200', ARRAY['new_id'], 'Check for new primary key in test_reindex_p1200');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1300', ARRAY['new_id'], 'Check for new primary key in test_reindex_p1300');
SELECT col_is_pk('partman_reindex_test', 'test_reindex_p1400', ARRAY['new_id'], 'Check for new primary key in test_reindex_p1400');

SELECT diag('!!! Now run reapply_index.py again with all the same arguments as last time but add --recreate_all !!!');
SELECT diag('!!! After that completes, run 04-check-indexes.sql !!!'); 

SELECT * FROM finish();
