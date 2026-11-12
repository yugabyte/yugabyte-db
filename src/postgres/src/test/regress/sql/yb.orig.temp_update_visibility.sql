--
-- Rows deleted or updated by the running transaction must stay deleted after
-- bottom-up index deletion, pruning, or ANALYZE checks them against the
-- vacuum horizon.
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

-- Pruning without an index: an earlier committed DELETE leaves a prune hint
-- on the page, and the page fills up inside the transaction
CREATE TEMP TABLE tp (k int, v text);
INSERT INTO tp SELECT g, repeat('x', 200) FROM generate_series(1, 28) g;
DELETE FROM tp WHERE k = 1;
BEGIN;
DELETE FROM tp WHERE k BETWEEN 2 AND 11;
INSERT INTO tp SELECT g, repeat('x', 200) FROM generate_series(101, 106) g;
SELECT count(*) FROM tp;
-- The page was pruned: the row deleted by the earlier DELETE is LP_DEAD
SELECT count(*) FROM heap_page_items(get_raw_page('tp', 0)) WHERE lp_flags = 3;
COMMIT;
SELECT count(*) FROM tp;

-- ANALYZE inside the transaction, without an index
CREATE TEMP TABLE ta (k int, v text);
INSERT INTO ta SELECT g, repeat('x', 200) FROM generate_series(1, 1000) g;
BEGIN;
DELETE FROM ta WHERE k <= 500;
ANALYZE ta;
SELECT count(*) FROM ta;
COMMIT;
SELECT count(*) FROM ta;

-- One transaction on a temp table and a YB table
CREATE TABLE ym (k int PRIMARY KEY, v text);
INSERT INTO ym SELECT g, 'x' FROM generate_series(1, 1000) g;
CREATE TEMP TABLE tm (k int PRIMARY KEY, v text);
INSERT INTO tm SELECT g, repeat('x', 200) FROM generate_series(1, 1000) g;
BEGIN;
DELETE FROM ym WHERE k <= 500;
UPDATE tm SET v = v || 'y';
UPDATE ym SET v = 'y';
SAVEPOINT s;
DELETE FROM ym WHERE k > 900;
DELETE FROM tm WHERE k > 900;
ROLLBACK TO s;
SELECT count(*), count(DISTINCT k), count(*) FILTER (WHERE v = 'y') FROM ym;
SELECT count(*), count(DISTINCT k) FROM tm;
COMMIT;
SELECT count(*), count(DISTINCT k), count(*) FILTER (WHERE v = 'y') FROM ym;
SELECT count(*), count(DISTINCT k) FROM tm;
DROP TABLE ym;

DROP EXTENSION pageinspect;
