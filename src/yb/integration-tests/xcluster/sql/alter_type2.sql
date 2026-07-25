--
-- ALTER_TYPE
--

-- Taken from Postgres test/regress/sql/yb.port.alter_table.sql

DROP TYPE test_type;

DROP TABLE test_tbl1;
DROP TYPE test_type1;

DROP TABLE test_tbl2_subclass, test_tbl2;
DROP TYPE test_type2;

DROP TABLE test_tblx;
DROP TYPE test_typex;

DROP TYPE test_type_empty;
