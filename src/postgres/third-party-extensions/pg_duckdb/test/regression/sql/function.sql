CREATE TABLE ta(a integer);
INSERT INTO ta VALUES(125);

-- test procedure
CREATE OR REPLACE PROCEDURE protest(b INT)
LANGUAGE plpgsql
AS $$
DECLARE
    va integer;
BEGIN
    SELECT * INTO va FROM ta WHERE a = b;
    RAISE NOTICE '%', va * 2;
END;
$$;

CALL protest(1);    -- null
CALL protest(125);  -- 250

-- test function
CREATE OR REPLACE FUNCTION functest(b INT)
RETURNS integer
LANGUAGE plpgsql
AS $$
DECLARE
    va integer;
BEGIN
    SELECT * INTO va FROM ta WHERE a = b;
    RETURN va * 2;
END;
$$;

SELECT functest(124);   -- null
SELECT functest(125);   -- 250

CREATE TABLE tb(a int DEFAULT 1, b text, c varchar DEFAULT 'pg_duckdb');
INSERT INTO tb(a, b) VALUES(1, 'test');
INSERT INTO tb VALUES(2, 'test2', 'pg_duckdb_test');

CREATE OR REPLACE FUNCTION functest2(va INT, vc varchar)
RETURNS text
LANGUAGE plpgsql
AS $$
DECLARE
    vb text;
BEGIN
    SELECT b INTO vb FROM tb WHERE a = va and c = vc;
    RETURN vb;
END;
$$;

SELECT functest2(1, 'pg_duckdb'); -- test

CREATE OR REPLACE PROCEDURE protest2(va INT, vb text)
LANGUAGE plpgsql
AS $$
DECLARE
    vc varchar;
BEGIN
    SELECT c INTO vc FROM tb WHERE a = va and b = vb;
    RAISE NOTICE '%', vc;
END;
$$;

CALL protest2(2, 'test2'); -- pg_duckdb_test

DROP TABLE ta, tb;
DROP FUNCTION functest, functest2;
DROP PROCEDURE protest, protest2;
