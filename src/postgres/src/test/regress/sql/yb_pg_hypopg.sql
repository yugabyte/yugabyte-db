-- The following comes from hypopg.sql
-- SETUP
CREATE OR REPLACE FUNCTION do_explain(stmt text) RETURNS table(a text) AS
$_$
DECLARE
    ret text;
BEGIN
    FOR ret IN EXECUTE format('EXPLAIN (FORMAT text) %s', stmt) LOOP
        a := ret;
        RETURN next ;
    END LOOP;
END;
$_$
LANGUAGE plpgsql;

CREATE EXTENSION hypopg;

CREATE TABLE hypo (id integer, val text);

INSERT INTO hypo SELECT i, 'line ' || i
FROM generate_series(1,100000) f(i);

-- The following comes from hypo_include.sql
-- hypothetical indexes using INCLUDE keyword, pg11+

-- Remove all the hypothetical indexes if any
SELECT hypopg_reset();

-- Create normal index
SELECT COUNT(*) AS NB
FROM hypopg_create_index('CREATE INDEX ON hypo (id)');

-- Should use hypothetical index using a regular Index Scan
SELECT COUNT(*) FROM do_explain('SELECT val FROM hypo WHERE id = 1') e
WHERE e ~ 'Index Scan.*<\d+>lsm_hypo.*';

-- Remove all the hypothetical indexes
SELECT hypopg_reset();

-- Create INCLUDE index
SELECT COUNT(*) AS NB
FROM hypopg_create_index('CREATE INDEX ON hypo (id) INCLUDE (val)');

-- Should use hypothetical index using an Index Only Scan
SELECT COUNT(*) FROM do_explain('SELECT val FROM hypo WHERE id = 1') e
WHERE e ~ 'Index Only Scan.*<\d+>lsm_hypo.*';

-- Deparse the index DDL
SELECT hypopg_get_indexdef(indexrelid) FROM hypopg();

-- The following comes from hypo_index_part.sql
-- Hypothetical on partitioned tables

CREATE TABLE hypo_part(id1 integer, id2 integer, id3 integer)
    PARTITION BY LIST (id1);
CREATE TABLE hypo_part_1
    PARTITION OF hypo_part FOR VALUES IN (1)
    PARTITION BY LIST (id2);
CREATE TABLE hypo_part_1_1
    PARTITION OF hypo_part_1 FOR VALUES IN (1);

INSERT INTO hypo_part SELECT 1, 1, generate_series(1, 10000);
SET enable_seqscan = 0;

-- hypothetical index on root partitioned table should work
SELECT COUNT(*) AS nb FROM hypopg_create_index('CREATE INDEX ON hypo_part (id3)');
SELECT 1, COUNT(*) FROM do_explain('SELECT * FROM hypo_part WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo_part.*';
SELECT hypopg_reset();

-- hypothetical index on non-root partitioned table should work
SELECT COUNT(*) AS nb FROM hypopg_create_index('CREATE INDEX ON hypo_part_1 (id3)');
SELECT 2, COUNT(*) FROM do_explain('SELECT * FROM hypo_part_1 WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo_part.*';
SELECT hypopg_reset();

-- hypothetical index on partition should work
SELECT COUNT(*) AS nb FROM  hypopg_create_index('CREATE INDEX ON hypo_part_1_1 (id3)');
SELECT 3, COUNT(*) FROM do_explain('SELECT * FROM hypo_part_1_1 WHERE id3 = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo_part.*';
