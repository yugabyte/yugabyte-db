-- Hypothetically hiding existing indexes tests

-- Remove all the hypothetical indexes if any
SELECT hypopg_reset();

-- The EXPLAIN initial state
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_idx';

-- Create real index in hypo and use this index
CREATE INDEX hypo_id_idx ON hypo(id);
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_idx';

-- Should be zero
SELECT COUNT(*) FROM hypopg_hidden_indexes();

-- The hypo_id_idx index should not be used
SELECT hypopg_hide_index('hypo_id_idx'::regclass);
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_idx';

-- Should be only one record
SELECT COUNT(*) FROM hypopg_hidden_indexes();
SELECT table_name,index_name FROM hypopg_hidden_indexes;

-- Create the real index again and
-- EXPLAIN should use this index instead of the previous one
CREATE index hypo_id_val_idx ON hypo(id, val);
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_val_idx';

-- Shouldn't use any index
SELECT hypopg_hide_index('hypo_id_val_idx'::regclass);
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_val_idx';

-- Should be two records
SELECT table_name,index_name FROM hypopg_hidden_indexes;

-- Try to add one repeatedly or add another wrong index oid
SELECT hypopg_hide_index('hypo_id_idx'::regclass);
SELECT hypopg_hide_index('hypo'::regclass);
SELECT hypopg_hide_index(0);

-- Also of course can be used to hide hypothetical indexes
SELECT COUNT(*) FROM hypopg_create_index('create index on hypo(id,val);');
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm
SELECT hypopg_hide_index((SELECT indexrelid FROM hypopg_list_indexes LIMIT 1));
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Should be only three records
SELECT COUNT(*) FROM hypopg_hidden_indexes;

-- Hypothetical indexes should be unhidden when deleting
SELECT hypopg_drop_index((SELECT indexrelid FROM hypopg_list_indexes LIMIT 1));

-- Should become two records
SELECT COUNT(*) FROM hypopg_hidden_indexes;

-- Hypopg_reset can also unhidden the hidden indexes
-- due to the deletion of hypothetical indexes.
SELECT COUNT(*) FROM hypopg_create_index('create index on hypo(id,val);');
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm
SELECT hypopg_hide_index((SELECT indexrelid FROM hypopg_list_indexes LIMIT 1));

-- Changed from three records to two records.
SELECT COUNT(*) FROM hypopg_hidden_indexes;
SELECT hypopg_reset();
SELECT COUNT(*) FROM hypopg_hidden_indexes;

-- Unhide an index
SELECT hypopg_unhide_index('hypo_id_idx'::regclass);
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'hypo_id_idx';

-- Should become one record
SELECT table_name,index_name FROM hypopg_hidden_indexes;

-- Try to delete one repeatedly or delete another wrong index oid
SELECT hypopg_unhide_index('hypo_id_idx'::regclass);
SELECT hypopg_unhide_index('hypo'::regclass);
SELECT hypopg_unhide_index(0);

-- Should still have one record
SELECT table_name,index_name FROM hypopg_hidden_indexes;

-- Unhide all indexes
SELECT hypopg_unhide_all_indexes();

-- Should change back to the original zero
SELECT COUNT(*) FROM hypopg_hidden_indexes();

-- Clean real indexes and hypothetical indexes
DROP INDEX hypo_id_idx;
DROP INDEX hypo_id_val_idx;
SELECT hypopg_reset();
