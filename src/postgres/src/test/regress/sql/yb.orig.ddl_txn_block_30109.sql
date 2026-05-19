-- #30109: duplicate key value violates unique constraint
-- The bug only exists when ysql_enable_concurrent_ddl = true.
-- We also test with ysql_enable_concurrent_ddl = false to ensure correctness.
-- The mode is set via the ysql_enable_concurrent_ddl gflag; this file is
-- run in both modes by TestPgRegressPgMisc.

CREATE TABLE test_table1();
CREATE OR REPLACE PROCEDURE test_alter1()
LANGUAGE plpgsql
AS $$
BEGIN
  EXECUTE 'ALTER TABLE test_table1 ADD COLUMN id_1 int DEFAULT (random() * 1000000)::int';
  COMMIT;
  EXECUTE 'ALTER TABLE test_table1 ADD COLUMN id_2 int DEFAULT (random() * 1000000)::int';
  COMMIT;
END;
$$;
CALL test_alter1();

DROP PROCEDURE test_alter1;
DROP TABLE test_table1;
