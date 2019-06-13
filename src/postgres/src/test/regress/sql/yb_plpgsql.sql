-- Test FUNCTION
CREATE OR REPLACE FUNCTION test_function()
RETURNS integer AS $$
BEGIN
   RETURN 10;
END; $$
LANGUAGE PLPGSQL;

SELECT * FROM test_function();

DROP FUNCTION test_function;

-- Test PROCEDURE
CREATE TABLE test_table(a INT);
CREATE OR REPLACE PROCEDURE test_procedure(val INT)
AS $$
BEGIN
   INSERT INTO test_table VALUES(val);
END; $$
LANGUAGE PLPGSQL;

CALL test_procedure(10);
SELECT * FROM test_table;

DROP ROUTINE test_procedure;

DROP PROCEDURE test_procedure; -- ERROR