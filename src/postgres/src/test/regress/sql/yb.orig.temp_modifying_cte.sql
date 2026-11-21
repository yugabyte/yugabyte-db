--
-- Writes to a temp table that is not the statement's top-level target
--
CREATE TEMP TABLE tt (k int PRIMARY KEY, v int);
INSERT INTO tt VALUES (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);

-- Data-modifying CTEs under a top-level SELECT
WITH d AS (DELETE FROM tt WHERE k = 1 RETURNING *) SELECT * FROM d;
WITH u AS (UPDATE tt SET v = 20 WHERE k = 2 RETURNING *) SELECT * FROM u;
WITH i AS (INSERT INTO tt VALUES (6, 6) RETURNING *) SELECT * FROM i;
BEGIN;
WITH d AS (DELETE FROM tt WHERE k = 3 RETURNING *) SELECT * FROM d;
COMMIT;
SELECT * FROM tt ORDER BY k;

-- Temp table written through a CTE while the top-level target is a YB table
CREATE TABLE yb_t (k int PRIMARY KEY, v int);
WITH d AS (DELETE FROM tt WHERE k = 4 RETURNING *) INSERT INTO yb_t SELECT * FROM d;
SELECT * FROM tt ORDER BY k;
SELECT * FROM yb_t ORDER BY k;

-- Prepared statement and PL/pgSQL
PREPARE cte_del(int) AS
  WITH d AS (DELETE FROM tt WHERE k = $1 RETURNING *) SELECT * FROM d;
EXECUTE cte_del(5);
EXECUTE cte_del(6);
SELECT * FROM tt ORDER BY k;
INSERT INTO tt VALUES (7, 7), (8, 8);
CREATE FUNCTION cte_upd(x int) RETURNS SETOF tt LANGUAGE plpgsql AS $$
BEGIN
  RETURN QUERY WITH u AS (UPDATE tt SET v = -v WHERE k = x RETURNING *)
    SELECT * FROM u;
END $$;
SELECT * FROM cte_upd(7);
SELECT * FROM tt ORDER BY k;

-- Rule on a YB table whose action writes a temp table
CREATE TEMP TABLE tt_log (k int, op text);
CREATE RULE yb_t_ins AS ON INSERT TO yb_t DO ALSO INSERT INTO tt_log VALUES (NEW.k, 'ins');
INSERT INTO yb_t VALUES (9, 9);
SELECT * FROM tt_log;

DROP FUNCTION cte_upd(int);
DROP TABLE yb_t;
