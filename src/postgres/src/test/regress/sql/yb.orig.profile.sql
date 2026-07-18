--
-- YB_PROFILE Testsuite: Testing statements for profiles.
--
--
-- pg_catalog alterations. Validate columns of pg_yb_profile and oids.
--
\d pg_yb_profile
SELECT oid, relname, reltype, relnatts FROM pg_class WHERE relname IN ('pg_yb_profile', 'pg_yb_profile_oid_index');
SELECT oid, typname, typrelid FROM pg_type WHERE typname LIKE 'pg_yb_profile';
SELECT pg_describe_object('pg_yb_profile'::regclass::oid, oid, 0) FROM pg_yb_profile;

--
-- CREATE PROFILE
--
SELECT oid, prfname, prfmaxfailedloginattempts FROM pg_catalog.pg_yb_profile ORDER BY oid;

CREATE PROFILE test_profile LIMIT FAILED_LOGIN_ATTEMPTS 3;

SELECT prfname, prfmaxfailedloginattempts FROM pg_catalog.pg_yb_profile ORDER BY OID;

-- Fail because it is a duplicate name
CREATE PROFILE test_profile LIMIT FAILED_LOGIN_ATTEMPTS 4;

-- Fail because -ve numbers are not allowed
CREATE PROFILE test_profile LIMIT FAILED_LOGIN_ATTEMPTS -1;

--
-- DROP PROFILE
--

DROP PROFILE test_profile;

-- fail: does not exist
DROP PROFILE test_profile;

--
-- Test IF EXISTS clause
--

DROP PROFILE IF EXISTS non_existing;

CREATE PROFILE exists_profile LIMIT FAILED_LOGIN_ATTEMPTS 3;
DROP PROFILE IF EXISTS exists_profile;

--
-- Test \h CREATE PROFILE
--

\h CREATE PROFILE

--
-- Test \h DROP PROFILE
--

\h DROP PROFILE

--
-- Test \h ALTER USER
--

\h ALTER USER

--
-- Test \h ALTER ROLE
--

\h ALTER ROLE