-- Using hints with hypothetical indexes

-- Remove all the hypothetical indexes if any
SELECT hypopg_reset();

-- Create a hypothetical index
SELECT COUNT(*) FROM hypopg_create_index('CREATE INDEX ON hypo (id ASC);');

-- Create a real index
CREATE INDEX hypo_real_idx ON hypo (id ASC);

-- Before #27927, the following query would crash.
/*+ IndexScan(hypo hypo_real_idx) */ EXPLAIN (costs off) SELECT * FROM hypo WHERE id = 1;