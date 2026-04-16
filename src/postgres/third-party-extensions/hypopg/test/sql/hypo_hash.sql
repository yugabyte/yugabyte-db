-- hypothetical hash indexes, pg10+

-- Remove all the hypothetical indexes if any
SELECT hypopg_reset();

-- Create normal index
SELECT COUNT(*) AS NB
FROM hypopg_create_index('CREATE INDEX ON hypo USING hash (id)');

-- Should use hypothetical index using a regular Index Scan
SELECT COUNT(*) FROM do_explain('SELECT val FROM hypo WHERE id = 1') e
WHERE e ~ 'Index Scan.*<\d+>hash_hypo.*';

-- Deparse the index DDL
SELECT hypopg_get_indexdef(indexrelid) FROM hypopg();
