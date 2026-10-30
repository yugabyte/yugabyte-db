--
-- Test yb_extension role
--
CREATE USER regress_priv_user;
CREATE OPERATOR FAMILY alt_opf1 USING hash;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto; -- should fail
CREATE EXTENSION orafce; -- should fail
\c -
GRANT yb_extension TO regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION pgcrypto;
ALTER EXTENSION pgcrypto UPDATE TO '1.4';
DROP EXTENSION pgcrypto;
CREATE EXTENSION pg_trgm WITH VERSION '1.3';
ALTER EXTENSION pg_trgm UPDATE TO '1.4';
DROP EXTENSION pg_trgm;
ALTER OPERATOR FAMILY alt_opf1 USING hash ADD -- should fail
  OPERATOR 1 < (int1, int2);
\c -
DROP USER regress_priv_user;

--
-- Test yb_fdw role
--
CREATE USER regress_priv_user1;
CREATE USER regress_priv_user2;
SET SESSION AUTHORIZATION regress_priv_user1;
CREATE FOREIGN DATA WRAPPER useless; -- should fail
\c -
GRANT yb_fdw TO regress_priv_user1 WITH ADMIN OPTION;
GRANT yb_extension TO regress_priv_user1;

SET SESSION AUTHORIZATION regress_priv_user1;
CREATE FOREIGN DATA WRAPPER useless;
ALTER FOREIGN DATA WRAPPER useless NO VALIDATOR;
ALTER FOREIGN DATA WRAPPER useless OWNER TO regress_priv_user2; -- should fail
GRANT yb_fdw TO regress_priv_user2;
ALTER FOREIGN DATA WRAPPER useless OWNER TO regress_priv_user2;
CREATE SERVER s1 FOREIGN DATA WRAPPER useless; -- should fail, since the owner changed
SET SESSION AUTHORIZATION regress_priv_user2;
CREATE SERVER s1 FOREIGN DATA WRAPPER useless;
ALTER SERVER s1 OPTIONS (host 'foo', dbname 'foodb');
DROP SERVER s1;
DROP FOREIGN DATA WRAPPER useless;

SET SESSION AUTHORIZATION regress_priv_user1;
CREATE EXTENSION file_fdw;
CREATE SERVER s1 FOREIGN DATA WRAPPER file_fdw;
-- should not allow the user to set the filename/program options as they require more privileges
CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS ( filename 'foo');
CREATE FOREIGN TABLE test (x int) SERVER s1 OPTIONS ( program 'foo');
DROP SERVER s1;
DROP EXTENSION file_fdw;

\c -
-- regress_priv_user1 granted yb_fdw to regress_priv_user2, so drop user2 first.
DROP USER regress_priv_user2;
DROP USER regress_priv_user1;

-- test password change with BYPASSRLS attribute
CREATE ROLE user1 BYPASSRLS;
ALTER ROLE user1 PASSWORD 'password';
DROP ROLE user1;

--
-- Test yb_db_admin role
--
-- verify yb_db_admin role can change bypassrls attribute
CREATE ROLE regress_test_user1;
SET SESSION ROLE yb_db_admin;
ALTER ROLE regress_test_user1 WITH BYPASSRLS;
CREATE ROLE regress_test_user2 WITH BYPASSRLS;
ALTER ROLE regress_test_user2 WITH NOBYPASSRLS;

-- clean up
SET SESSION ROLE yugabyte;
DROP ROLE regress_test_user1;
DROP ROLE regress_test_user2;

-- verify a member of yb_db_admin without CREATEROLE can change bypassrls
-- attribute. Unlike the test above, it runs as a member, not as yb_db_admin
-- itself. The superuser creates regress_test_user3, so the member has no
-- ADMIN OPTION on it.
CREATE ROLE regress_test_user3;
CREATE ROLE regress_test_admin IN ROLE yb_db_admin;
SET SESSION AUTHORIZATION regress_test_admin;
CREATE ROLE regress_test_user4 WITH BYPASSRLS;
ALTER ROLE regress_test_user3 WITH BYPASSRLS;
ALTER ROLE regress_test_user3 WITH NOBYPASSRLS;

-- clean up
RESET SESSION AUTHORIZATION;
DROP ROLE regress_test_user3;
DROP ROLE regress_test_user4;
DROP ROLE regress_test_admin;

-- verify upstream's rules apply to members of yb_db_admin. regress_test_admin
-- has CREATEROLE but not CREATEDB, and ADMIN OPTION on regress_test_user6 but
-- not on regress_test_user5. The superuser creates both.
CREATE ROLE regress_test_user5;
CREATE ROLE regress_test_user6;
CREATE ROLE regress_test_admin CREATEROLE IN ROLE yb_db_admin;
GRANT regress_test_user6 TO regress_test_admin WITH ADMIN OPTION;
SET SESSION AUTHORIZATION regress_test_admin;
-- giving CREATEDB needs CREATEDB, even with ADMIN OPTION on the role
CREATE ROLE regress_test_user7 WITH CREATEDB; -- should fail
ALTER ROLE regress_test_user6 WITH CREATEDB; -- should fail
-- altering another role needs ADMIN OPTION on it
ALTER ROLE regress_test_user5 WITH PASSWORD 'password'; -- should fail

-- clean up
RESET SESSION AUTHORIZATION;
DROP ROLE regress_test_user5;
DROP ROLE regress_test_user6;
DROP ROLE regress_test_admin;

--
-- Test YB Managed admin role
--
RESET SESSION AUTHORIZATION;
CREATE USER regress_priv_user;
GRANT yb_extension TO regress_priv_user;
GRANT yb_fdw TO regress_priv_user;
GRANT yb_db_admin TO regress_priv_user WITH ADMIN OPTION;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION PGAudit;
ALTER EXTENSION PGAudit UPDATE TO '19.0';
DROP EXTENSION PGAudit;
CREATE EXTENSION orafce;
ALTER EXTENSION orafce UPDATE TO '4.16';
DROP EXTENSION orafce;
-- removing yb_db_admin role should result in error
REVOKE yb_db_admin FROM regress_priv_user;
-- Since PG 16 the REVOKE above only removes grants that regress_priv_user made,
-- and a superuser made this one, so the superuser revokes it.
RESET SESSION AUTHORIZATION;
REVOKE yb_db_admin FROM regress_priv_user;
SET SESSION AUTHORIZATION regress_priv_user;
CREATE EXTENSION PGAudit;
DROP EXTENSION PGAudit;

--
-- Test yugabyte as a bootstrap superuser, like postgres
--
RESET SESSION AUTHORIZATION;
SELECT current_user;
-- yugabyte has its fixed OID and the attributes initdb used to give it
SELECT oid, rolsuper, rolinherit, rolcreaterole, rolcreatedb, rolcanlogin,
       rolreplication, rolbypassrls
  FROM pg_authid WHERE rolname = 'yugabyte';
CREATE ROLE regress_yb_role;
CREATE ROLE regress_yb_member;
CREATE ROLE regress_yb_admin;
GRANT regress_yb_role TO regress_yb_admin WITH ADMIN OPTION;
-- yugabyte can name itself as the grantor; the grant is recorded under postgres
GRANT regress_yb_role TO regress_yb_member WITH ADMIN OPTION GRANTED BY CURRENT_ROLE;
GRANT regress_yb_role TO regress_yb_member GRANTED BY yugabyte;
SELECT member::regrole, grantor::regrole, admin_option FROM pg_auth_members
  WHERE roleid = 'regress_yb_role'::regrole ORDER BY member::regrole::text;
-- a plain REVOKE removes that grant
REVOKE regress_yb_role FROM regress_yb_member;
SELECT pg_has_role('regress_yb_member', 'regress_yb_role', 'MEMBER');
-- a non-superuser cannot give ADMIN OPTION to a bootstrap superuser
SET ROLE regress_yb_admin;
GRANT regress_yb_role TO yugabyte WITH ADMIN OPTION; -- error
GRANT regress_yb_role TO postgres WITH ADMIN OPTION; -- error
RESET ROLE;
-- REVOKE ... GRANTED BY yugabyte removes a grant recorded under postgres
GRANT regress_yb_role TO regress_yb_member GRANTED BY yugabyte;
REVOKE regress_yb_role FROM regress_yb_member GRANTED BY yugabyte;
SELECT pg_has_role('regress_yb_member', 'regress_yb_role', 'MEMBER');
-- a non-superuser that grants through ADMIN inherited from yugabyte records
-- the grant under yugabyte, and the grant depends on that ADMIN
CREATE ROLE regress_yb_inheritor;
GRANT regress_yb_role TO yugabyte WITH ADMIN OPTION;
GRANT yugabyte TO regress_yb_inheritor;
SET ROLE regress_yb_inheritor;
GRANT regress_yb_role TO regress_yb_member;
RESET ROLE;
GRANT regress_yb_role TO regress_yb_member;
SELECT member::regrole, grantor::regrole FROM pg_auth_members
  WHERE roleid = 'regress_yb_role'::regrole
    AND member = 'regress_yb_member'::regrole
  ORDER BY grantor::regrole::text;
REVOKE ADMIN OPTION FOR regress_yb_role FROM yugabyte; -- error
-- a superuser's REVOKE removes the grants under both postgres and yugabyte
REVOKE regress_yb_role FROM regress_yb_member;
SELECT pg_has_role('regress_yb_member', 'regress_yb_role', 'MEMBER');
SET ROLE regress_yb_inheritor;
GRANT regress_yb_role TO regress_yb_member;
RESET ROLE;
REVOKE regress_yb_role FROM regress_yb_member GRANTED BY yugabyte;
SELECT pg_has_role('regress_yb_member', 'regress_yb_role', 'MEMBER');
-- yugabyte cannot lose SUPERUSER, be renamed or dropped, or give up what it owns
SET SESSION AUTHORIZATION postgres;
ALTER ROLE yugabyte NOSUPERUSER; -- error
ALTER ROLE yugabyte RENAME TO regress_yb_renamed; -- error
DROP ROLE yugabyte; -- error
REASSIGN OWNED BY yugabyte TO regress_yb_member; -- error
DROP OWNED BY yugabyte; -- error
RESET SESSION AUTHORIZATION;
DROP ROLE regress_yb_role, regress_yb_member, regress_yb_admin,
  regress_yb_inheritor;
