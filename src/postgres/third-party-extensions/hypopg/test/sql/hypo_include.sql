-- hypothetical indexes using INCLUDE keyword, pg11+

-- Remove all the hypothetical indexes if any
SELECT hypopg_reset();

-- Make sure stats and visibility map are up to date
VACUUM ANALYZE hypo;

-- Should not use hypothetical index

-- Create normal index
SELECT COUNT(*) AS NB
FROM hypopg_create_index('CREATE INDEX ON hypo (id)');

-- Should use hypothetical index using a regular Index Scan
SELECT COUNT(*) FROM do_explain('SELECT val FROM hypo WHERE id = 1') e
WHERE e ~ 'Index Scan.*<\d+>btree_hypo.*';

-- Remove all the hypothetical indexes
SELECT hypopg_reset();

-- Create INCLUDE index
SELECT COUNT(*) AS NB
FROM hypopg_create_index('CREATE INDEX ON hypo (id) INCLUDE (val)');

-- Should use hypothetical index using an Index Only Scan
SELECT COUNT(*) FROM do_explain('SELECT val FROM hypo WHERE id = 1') e
WHERE e ~ 'Index Only Scan.*<\d+>btree_hypo.*';

-- Deparse the index DDL
SELECT hypopg_get_indexdef(indexrelid) FROM hypopg();
