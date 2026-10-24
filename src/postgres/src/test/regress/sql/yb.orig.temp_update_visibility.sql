--
-- Rows deleted or updated by the running statement must stay deleted after
-- the statement prunes the pages they are on.
--
CREATE EXTENSION pageinspect;

-- UPDATE of many rows in an indexed temp table
CREATE TEMP TABLE tu (k int PRIMARY KEY, v text);
INSERT INTO tu SELECT g, repeat('x', 200) FROM generate_series(1, 1000) g;
UPDATE tu SET v = v || 'y';
SELECT count(*), count(DISTINCT k), sum(length(v)) FROM tu;
-- Old versions must not carry HEAP_XMAX_INVALID (0x0800)
SELECT count(*) FROM heap_page_items(get_raw_page('tu', 0))
  WHERE t_xmax <> 0 AND (t_infomask & 2048) <> 0;

-- Same through a secondary index
CREATE TEMP TABLE ti (k int, v text);
CREATE INDEX ON ti (k);
INSERT INTO ti SELECT g, repeat('x', 200) FROM generate_series(1, 1000) g;
UPDATE ti SET v = v || 'y' WHERE k % 2 = 0;
SELECT count(*), count(DISTINCT k) FROM ti;

-- DELETE and re-read within one transaction, including a savepoint
BEGIN;
DELETE FROM tu WHERE k <= 500;
SELECT count(*) FROM tu;
SAVEPOINT s;
UPDATE tu SET v = 'z' WHERE k > 900;
ROLLBACK TO s;
UPDATE tu SET v = 'w' WHERE k > 800;
SELECT count(*), count(DISTINCT k) FROM tu;
COMMIT;
SELECT count(*), count(DISTINCT k), count(*) FILTER (WHERE v = 'w') FROM tu;

DROP EXTENSION pageinspect;
