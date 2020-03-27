--
-- YB_TABLE Testsuite: Testing DDL Statments for TABLE DROP.
--
-- Test: Drop Multiple Tables
CREATE TABLE table_drop_test1(a int, b int);
CREATE TABLE table_drop_test2(c int, d int);
DROP TABLE table_drop_test1, table_drop_test2;
--
-- Test: Drop Too Many Tables and Ensure Error
CREATE TABLE table_drop_test1(a int, b int);
DROP TABLE table_drop_test1, table_drop_test2;
INSERT INTO table_drop_test1 VALUES(1,2);
--
-- Test: Drop Too Many Tables IF EXISTS and Ensure NOTICE
DROP TABLE IF EXISTS table_drop_test1, table_drop_test2;
INSERT INTO table_drop_test1 VALUES(1,2);
--
