SET duckdb.force_execution = true;
CREATE TABLE ta(a INT);
EXPLAIN SELECT count(*) FROM ta;
CALL duckdb.recycle_ddb();
EXPLAIN SELECT count(*) FROM ta;

-- Not allowed in a transaction
BEGIN;
CALL duckdb.recycle_ddb();
END;

-- Nor in a function
CREATE OR REPLACE FUNCTION f() RETURNS void
    LANGUAGE plpgsql
    RETURNS NULL ON NULL INPUT
    AS
$$
BEGIN
    CALL duckdb.recycle_ddb();
END;
$$;
SET duckdb.force_execution = false;
SELECT * FROM f();

DROP TABLE ta;
DROP FUNCTION f;
