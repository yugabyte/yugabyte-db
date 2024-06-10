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

CREATE TABLE hypo (id integer, val text, "Id2" bigint);

INSERT INTO hypo SELECT i, 'line ' || i
FROM generate_series(1,100000) f(i);

ANALYZE hypo;

-- TESTS
SELECT COUNT(*) AS nb
FROM public.hypopg_create_index('SELECT 1;CREATE INDEX ON hypo(id ASC); SELECT 2'); -- YB: add ASC to match upstream behavior

SELECT schema_name, table_name, am_name FROM public.hypopg_list_indexes; -- YB: output has lsm instead of btree

-- Should use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Should use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo ORDER BY id') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Should not use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Add predicate index
SELECT COUNT(*) AS nb
FROM public.hypopg_create_index('CREATE INDEX ON hypo(id ASC) WHERE id < 5'); -- YB: add ASC to match upstream behavior

-- This specific index should be used
WITH ind AS (
    SELECT indexrelid, row_number() OVER (ORDER BY indexrelid) AS num
    FROM public.hypopg()
),
regexp AS (
    SELECT regexp_replace(e, '.*<(\d+)>.*', E'\\1', 'g') AS r
    FROM do_explain('SELECT * FROM hypo WHERE id < 3') AS e
)

SELECT num
FROM ind
JOIN regexp ON ind.indexrelid::text = regexp.r;

-- Specify fillfactor
SELECT COUNT(*) AS NB
FROM public.hypopg_create_index('CREATE INDEX ON hypo(id) WITH (fillfactor = 10)');
SELECT COUNT(*) AS NB -- YB: create same index without fillfactor as a workaround
FROM public.hypopg_create_index('CREATE INDEX ON hypo(id ASC)'); -- YB: add ASC to match upstream behavior

-- Specify an incorrect fillfactor
SELECT COUNT(*) AS NB
FROM public.hypopg_create_index('CREATE INDEX ON hypo(id) WITH (fillfactor = 1)');

-- Index size estimation
SELECT hypopg_relation_size(indexrelid) = current_setting('block_size')::bigint AS one_block
FROM hypopg()
ORDER BY indexrelid; -- YB: TODO: figure out why output differs

-- Should detect invalid argument
SELECT hypopg_relation_size(1);

-- locally disable hypoopg
SET hypopg.enabled to false;

-- no hypothetical index should be used
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- locally re-enable hypoopg
SET hypopg.enabled to true;

-- hypothetical index should be used
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Remove one hypothetical index
SELECT hypopg_drop_index(indexrelid) FROM hypopg() ORDER BY indexrelid LIMIT 1;

-- Remove all the hypothetical indexes
SELECT hypopg_reset();

-- index on expression
SELECT COUNT(*) AS NB
FROM public.hypopg_create_index('CREATE INDEX ON hypo (md5(val))');

-- Should use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE md5(val) = md5(''line 1'')') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

-- Deparse an index DDL, with almost every possible pathcode
SELECT hypopg_get_indexdef(indexrelid) FROM hypopg_create_index('create index on hypo using btree(id desc, "Id2" desc nulls first, id desc nulls last, cast(md5(val) as bpchar)  bpchar_pattern_ops) with (fillfactor = 10) WHERE id < 1000 AND id +1 %2 = 3');
SELECT hypopg_get_indexdef(indexrelid) FROM hypopg_create_index('create index on hypo using btree(id desc, "Id2" desc nulls first, id desc nulls last, cast(md5(val) as bpchar)  bpchar_pattern_ops) WHERE id < 1000 AND id +1 %2 = 3'); -- YB: create same index without fillfactor as a workaround

-- Make sure the old Oid generator still works.  Test it while keeping existing
-- entries, as both should be able to coexist.
SET hypopg.use_real_oids = on;

-- Should not use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm

SELECT COUNT(*) AS nb
FROM public.hypopg_create_index('CREATE INDEX ON hypo(id);');

-- Should use hypothetical index
SELECT COUNT(*) FROM do_explain('SELECT * FROM hypo WHERE id = 1') e
WHERE e ~ 'Index.*<\d+>lsm_hypo.*'; -- YB: change btree to lsm
