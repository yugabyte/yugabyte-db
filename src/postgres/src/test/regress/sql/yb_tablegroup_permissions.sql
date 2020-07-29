--
-- YB_TABLEGROUP Testsuite: Testing permissions for TABLEGROUP.
--
CREATE DATABASE test_tablegroup;
\c test_tablegroup

--
-- CREATE TABLEGROUP
--
CREATE USER user_1;
CREATE USER user_2 SUPERUSER;
CREATE USER user_3;
GRANT CREATE ON DATABASE test_tablegroup TO user_3;
CREATE TABLEGROUP owned_by1 OWNER user_1;
CREATE TABLEGROUP supergroup;

--
-- Operations with user_3
--
\c test_tablegroup user_3
-- Create tablegroup should succeed as well as dropping that tablegroup.
CREATE TABLEGROUP owned_by3;
DROP TABLEGROUP owned_by3;
CREATE TABLEGROUP owned_by1_created_by3 OWNER user_1;
-- However these should fail.
DROP TABLEGROUP supergroup;
DROP TABLEGROUP owned_by1;

--
-- Operations with user_2
--
\c test_tablegroup user_2
-- All operations should succeed.
CREATE TABLEGROUP owned_by2;
CREATE TABLEGROUP owned_by1_created_by2 OWNER user_1;
DROP TABLEGROUP supergroup;
CREATE TABLE a(i text) TABLEGROUP owned_by2;

--
-- Operations with user_1
--
\c test_tablegroup user_1
-- These will fail
DROP TABLEGROUP owned_by2;
CREATE TABLEGROUP owned_by1_created_by1;
-- However these should succed
DROP TABLEGROUP owned_by1;
DROP TABLEGROUP owned_by1_created_by2;
CREATE TABLE b(i text) TABLEGROUP owned_by1_created_by3;
-- Create table/index with a tablegroup should default to no privs. Only owner/superuser.
-- This should fail.
CREATE TABLE c(i text) TABLEGROUP owned_by2;
