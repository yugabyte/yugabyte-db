--
-- YSQL database cluster dump
--

SET default_transaction_read_only = off;

SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;

--
-- Roles
--

-- Set variable ignore_existing_roles (if not already set)
\if :{?ignore_existing_roles}
\else
\set ignore_existing_roles false
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'postgres') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' postgres
\else
    CREATE ROLE postgres;
    ALTER ROLE postgres WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS;
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user
\else
    CREATE ROLE test_user;
    ALTER ROLE test_user WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'md5d5dec22d2a9201f8b9825e21f6db97e4';
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user2') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user2
\else
    CREATE ROLE test_user2;
    ALTER ROLE test_user2 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'md54a5ddcd4bacb5359d2c91591a02a3fcc';
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user3') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user3
\else
    CREATE ROLE test_user3;
    ALTER ROLE test_user3 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'md57ed39e43212e93ec7037f51812d89f1b';
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yb_db_admin') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' yb_db_admin
\else
    CREATE ROLE yb_db_admin;
    ALTER ROLE yb_db_admin WITH NOSUPERUSER NOINHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yb_extension') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' yb_extension
\else
    CREATE ROLE yb_extension;
    ALTER ROLE yb_extension WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yb_fdw') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' yb_fdw
\else
    CREATE ROLE yb_fdw;
    ALTER ROLE yb_fdw WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
\endif

ALTER ROLE yugabyte WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS PASSWORD 'md52c2dc7d65d3e364f08b8addff5a54bf5';

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' yugabyte_test
\else
    CREATE ROLE yugabyte_test;
    ALTER ROLE yugabyte_test WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN NOREPLICATION BYPASSRLS;
\endif


--
-- User Configurations
--






--
-- YB Profiles
--

CREATE PROFILE profile_2_failed LIMIT FAILED_LOGIN_ATTEMPTS 2;
CREATE PROFILE profile_3_failed LIMIT FAILED_LOGIN_ATTEMPTS 3;


--
-- YB Role-Profile Mappings
--

ALTER ROLE test_user PROFILE profile_3_failed;
UPDATE pg_catalog.pg_yb_role_profile
SET rolprfstatus = 'l',
    rolprffailedloginattempts = 4
WHERE rolprfrole = (SELECT oid FROM pg_authid WHERE rolname = 'test_user')
  AND rolprfprofile = (SELECT oid FROM pg_yb_profile WHERE prfname = 'profile_3_failed');

ALTER ROLE test_user2 PROFILE profile_3_failed;
UPDATE pg_catalog.pg_yb_role_profile
SET rolprfstatus = 'o',
    rolprffailedloginattempts = 0
WHERE rolprfrole = (SELECT oid FROM pg_authid WHERE rolname = 'test_user2')
  AND rolprfprofile = (SELECT oid FROM pg_yb_profile WHERE prfname = 'profile_3_failed');

ALTER ROLE test_user3 PROFILE profile_2_failed;
UPDATE pg_catalog.pg_yb_role_profile
SET rolprfstatus = 'o',
    rolprffailedloginattempts = 1
WHERE rolprfrole = (SELECT oid FROM pg_authid WHERE rolname = 'test_user3')
  AND rolprfprofile = (SELECT oid FROM pg_yb_profile WHERE prfname = 'profile_2_failed');


--
-- Databases
--

--
-- Database "template1" dump
--

\connect template1

--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.27.0.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.27.0.0-b0

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

-- Set variable use_tablespaces (if not already set)
\if :{?use_tablespaces}
\else
\set use_tablespaces true
\endif

-- Set variable use_roles (if not already set)
\if :{?use_roles}
\else
\set use_roles true
\endif

-- YB: disable auto analyze to avoid conflicts with catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO on', current_database());
  END IF;
END $$;

--
-- Name: FUNCTION pg_stat_statements_reset(userid oid, dbid oid, queryid bigint); Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset(userid oid, dbid oid, queryid bigint) FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements_info; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements_info TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


-- YB: re-enable auto analyze after all catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO off', current_database());
  END IF;
END $$;

--
-- YSQL database dump complete
--

--
-- Database "postgres" dump
--

\connect postgres

--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.27.0.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.27.0.0-b0

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

-- Set variable use_tablespaces (if not already set)
\if :{?use_tablespaces}
\else
\set use_tablespaces true
\endif

-- Set variable use_roles (if not already set)
\if :{?use_roles}
\else
\set use_roles true
\endif

-- YB: disable auto analyze to avoid conflicts with catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO on', current_database());
  END IF;
END $$;

--
-- Name: FUNCTION pg_stat_statements_reset(userid oid, dbid oid, queryid bigint); Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset(userid oid, dbid oid, queryid bigint) FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements_info; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements_info TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


-- YB: re-enable auto analyze after all catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO off', current_database());
  END IF;
END $$;

--
-- YSQL database dump complete
--

--
-- Database "system_platform" dump
--

--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.27.0.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.27.0.0-b0

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

-- Set variable use_tablespaces (if not already set)
\if :{?use_tablespaces}
\else
\set use_tablespaces true
\endif

-- Set variable use_roles (if not already set)
\if :{?use_roles}
\else
\set use_roles true
\endif

--
-- Name: system_platform; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE system_platform WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\if :use_roles
    ALTER DATABASE system_platform OWNER TO postgres;
\endif

\connect system_platform

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: DATABASE system_platform; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON DATABASE system_platform IS 'system database for YugaByte platform';


--
-- Name: FUNCTION pg_stat_statements_reset(userid oid, dbid oid, queryid bigint); Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset(userid oid, dbid oid, queryid bigint) FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements_info; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements_info TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- YSQL database dump complete
--

--
-- Database "yugabyte" dump
--

--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.27.0.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.27.0.0-b0

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

-- Set variable use_tablespaces (if not already set)
\if :{?use_tablespaces}
\else
\set use_tablespaces true
\endif

-- Set variable use_roles (if not already set)
\if :{?use_roles}
\else
\set use_roles true
\endif

--
-- Name: yugabyte; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE yugabyte WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\if :use_roles
    ALTER DATABASE yugabyte OWNER TO postgres;
\endif

\connect yugabyte

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_ignore_relfilenode_ids') THEN
    EXECUTE 'SET yb_ignore_relfilenode_ids TO false';
  END IF;
END $$;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: DATABASE yugabyte; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON DATABASE yugabyte IS 'default administrative connection database';


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: pg_database_owner
--

\if :use_roles
REVOKE USAGE ON SCHEMA public FROM PUBLIC;
GRANT ALL ON SCHEMA public TO PUBLIC;
\endif


--
-- Name: FUNCTION pg_stat_statements_reset(userid oid, dbid oid, queryid bigint); Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset(userid oid, dbid oid, queryid bigint) FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE pg_stat_statements_info; Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements_info TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- YSQL database dump complete
--

--
-- YSQL database cluster dump complete
--

