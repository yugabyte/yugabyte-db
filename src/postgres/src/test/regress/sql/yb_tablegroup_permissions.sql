--
-- YB_TABLEGROUP Testsuite: Testing permissions for TABLEGROUP.
--
CREATE DATABASE test_tablegroup;
\c test_tablegroup

--
-- CREATE TABLEGROUP
--
CREATE USER user_1;
GRANT CREATE ON SCHEMA public TO user_1;
CREATE USER user_2 SUPERUSER;
CREATE USER user_3;
GRANT CREATE ON SCHEMA public TO user_3;
CREATE USER user_4;
GRANT CREATE ON SCHEMA public TO user_4;
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
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLEGROUP supergroup;
DROP TABLEGROUP owned_by1;
\set VERBOSITY default

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
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLEGROUP owned_by2;
\set VERBOSITY default
CREATE TABLEGROUP owned_by1_created_by1;
-- However these should succed
DROP TABLEGROUP owned_by1;
DROP TABLEGROUP owned_by1_created_by2;
CREATE TABLE b(i text) TABLEGROUP owned_by1_created_by3;
-- Create table/index with a tablegroup should default to no privs. Only owner/superuser.
-- This should fail.
CREATE TABLE c(i text) TABLEGROUP owned_by2;

--
-- GRANT/REVOKE tests
--
\c test_tablegroup yugabyte
-- Grant privs to user_1 and user_3 and revoke from user_2 (but still test that superuser supersedes)
CREATE TABLEGROUP test_grant;
GRANT CREATE ON TABLEGROUP test_grant TO user_1 WITH GRANT OPTION;
REVOKE ALL ON TABLEGROUP test_grant FROM user_2;
GRANT ALL ON TABLEGROUP test_grant TO user_3;

--
-- Operations with user_4 (no perms)
--
\c test_tablegroup user_4
-- This will fail
CREATE TABLE d(i text) TABLEGROUP test_grant;

--
-- Operations with user_3 (all perms = ACL_CREATE)
--
\c test_tablegroup user_3
-- This will succeed
CREATE TABLE e(i text) TABLEGROUP test_grant;
CREATE INDEX ON e(i);

--
-- Operations with user_2 (no direct perms but superuser should bypass aclcheck)
--
\c test_tablegroup user_2
-- This will succeed
CREATE TABLE f(i text) TABLEGROUP test_grant;
CREATE INDEX ON f(i);

--
-- Operations with user_1 (create perms), also has GRANT OPTION
--
\c test_tablegroup user_1
-- This will also succeed
CREATE TABLE g(i text) TABLEGROUP test_grant;
GRANT CREATE ON TABLEGROUP test_grant TO user_4;

--
-- Operations with user_4 - now has CREATE
--
\c test_tablegroup user_4
-- This will now succeed
CREATE TABLE d(i text) TABLEGROUP test_grant;

--
-- Now REVOKE privs and ensure the users no longer have access.
--
\c test_tablegroup yugabyte
REVOKE CREATE ON TABLEGROUP test_grant FROM user_1 CASCADE;
REVOKE GRANT OPTION FOR CREATE ON TABLEGROUP test_grant FROM user_1;
REVOKE ALL ON TABLEGROUP test_grant FROM user_3;

--
-- Operations with user_3 (no perms now)
--
\c test_tablegroup user_3
-- This will fail
CREATE TABLE h(i text) TABLEGROUP test_grant;
CREATE INDEX ON e(i);

--
-- Operations with user_1 (no perms now, no grant option)
--
\c test_tablegroup user_1
-- This will fail
CREATE TABLE i(i text) TABLEGROUP test_grant;
GRANT CREATE ON TABLEGROUP test_grant TO user_3;

--
-- ALTER DEFAULT PRIVILEGES tests
--
\c yugabyte yugabyte
-- Grant user_1 CREATE for all future tablegroups
ALTER DEFAULT PRIVILEGES GRANT CREATE ON TABLEGROUPS TO user_1;
CREATE TABLEGROUP test_adef;
\ddp
-- Operations with user_1. Should work without explicit grant for test_adef.
\c yugabyte user_1
CREATE TABLE i(i text) TABLEGROUP test_adef;
-- Revoke user_1 CREATE default
\c yugabyte yugabyte
ALTER DEFAULT PRIVILEGES REVOKE CREATE ON TABLEGROUPS FROM user_1;
\ddp
