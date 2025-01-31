-- Hypothetical on partitioned tables

CREATE TABLE hypo_part(id1 integer, id2 integer, id3 integer)
    PARTITION BY LIST (id1);
CREATE TABLE hypo_part_1
    PARTITION OF hypo_part FOR VALUES IN (1)
    PARTITION BY LIST (id2);
CREATE TABLE hypo_part_1_1
    PARTITION OF hypo_part_1 FOR VALUES IN (1);

INSERT INTO hypo_part SELECT 1, 1, generate_series(1, 10000);
ANALYZE hypo_part;
SET enable_seqscan = 0;

-- hypothetical index on root partitioned table should work
SELECT COUNT(*) AS nb FROM hypopg_create_index('CREATE INDEX ON hypo_part (id3)');
SELECT 1, COUNT(*) FROM do_explain('SELECT * FROM hypo_part WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>btree_hypo_part.*';
SELECT hypopg_reset();

-- hypothetical index on non-root partitioned table should work
SELECT COUNT(*) AS nb FROM hypopg_create_index('CREATE INDEX ON hypo_part_1 (id3)');
SELECT 2, COUNT(*) FROM do_explain('SELECT * FROM hypo_part_1 WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>btree_hypo_part.*';
SELECT hypopg_reset();

-- hypothetical index on partition should work
SELECT COUNT(*) AS nb FROM  hypopg_create_index('CREATE INDEX ON hypo_part_1_1 (id3)');
SELECT 3, COUNT(*) FROM do_explain('SELECT * FROM hypo_part_1_1 WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>btree_hypo_part.*';
