--
-- YSQL database cluster dump
--

\restrict <key>

SET default_transaction_read_only = off;

SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;

--
-- Roles
--

\unrestrict <key>
-- Set variable ignore_existing_roles (if not already set)
\if :{?ignore_existing_roles}
\else
\set ignore_existing_roles false
\endif
\restrict <key>

\unrestrict <key>
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
\restrict <key>

\unrestrict <key>
\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user
\else
    CREATE ROLE test_user;
    ALTER ROLE test_user WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'SCRAM-SHA-256$4096:L31zI6UdQSK7leP2nCKiGA==$4B43kvJktwHnPRDqqROPEeKMTqFO0/lLNpIhiUwTpUM=:eb+xIhw8wtr+RZS540Op3WwbAmw7YSjFbqkR8x0QzK4=';
\endif
\restrict <key>

\unrestrict <key>
\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user2') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user2
\else
    CREATE ROLE test_user2;
    ALTER ROLE test_user2 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'SCRAM-SHA-256$4096:8ZkGcQZBK1XaH5/awp31tQ==$qL/fKeY9irjxA1ugY72SaT9/2MDp6smHTcpRaZFiOcA=:hnF/fprjqtDEBDqaSSqJxPLM4ExQ7fvAiJpBaWvECTU=';
\endif
\restrict <key>

\unrestrict <key>
\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'test_user3') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' test_user3
\else
    CREATE ROLE test_user3;
    ALTER ROLE test_user3 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS PASSWORD 'SCRAM-SHA-256$4096:8x4hoGmQZA9Zyq38YqIErw==$7I00T4qJ/UYWQZ7wfPLy/vrw926mgovnyvUl1GPBD0w=:8842iXKyGQlZaZNskV9MgSBxcdJ9EzpD8N+KNYfRr5E=';
\endif
\restrict <key>

\unrestrict <key>
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
\restrict <key>

\unrestrict <key>
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
\restrict <key>

\unrestrict <key>
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
\restrict <key>

ALTER ROLE yugabyte WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS PASSWORD 'SCRAM-SHA-256$4096:VLK4RMaQLCvNtQ==$6YtlR4t69SguDiwFvbVgVZtuz6gpJQQqUMZ7IQJK5yI=:ps75jrHeYU4lXCcXI4O8oIdJ3eO8o2jirjruw9phBTo=';

\unrestrict <key>
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
\restrict <key>


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


\unrestrict <key>

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

\restrict <key>

-- Dumped from database version 19devel-YB-2.31.0.1900-b0
-- Dumped by ysql_dump version 19devel-YB-2.31.0.1900-b0

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;
\unrestrict <key>

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
\restrict <key>

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

\unrestrict <key>

--
-- Database "postgres" dump
--

\connect postgres

--
-- YSQL database dump
--

\restrict <key>

-- Dumped from database version 19devel-YB-2.31.0.1900-b0
-- Dumped by ysql_dump version 19devel-YB-2.31.0.1900-b0

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;
\unrestrict <key>

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
\restrict <key>

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

\unrestrict <key>

--
-- Database "system_platform" dump
--

--
-- YSQL database dump
--

\restrict <key>

-- Dumped from database version 19devel-YB-2.31.0.1900-b0
-- Dumped by ysql_dump version 19devel-YB-2.31.0.1900-b0

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;
\unrestrict <key>

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
\restrict <key>

--
-- Name: system_platform; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE system_platform WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\unrestrict <key>
\if :use_roles
    ALTER DATABASE system_platform OWNER TO postgres;
\endif

\restrict <key>
\unrestrict <key>
\connect system_platform
\restrict <key>

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;


-- YB: preserve restored reltuples during index creation
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_enable_update_reltuples_after_create_index') THEN
    EXECUTE 'SET yb_enable_update_reltuples_after_create_index TO false';
  END IF;
END $$;
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

\unrestrict <key>

--
-- Database "yugabyte" dump
--

--
-- YSQL database dump
--

\restrict <key>

-- Dumped from database version 19devel-YB-2.31.0.1900-b0
-- Dumped by ysql_dump version 19devel-YB-2.31.0.1900-b0

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;
\unrestrict <key>

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
\restrict <key>

--
-- Name: yugabyte; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE yugabyte WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\unrestrict <key>
\if :use_roles
    ALTER DATABASE yugabyte OWNER TO postgres;
\endif

\restrict <key>
\unrestrict <key>
\connect yugabyte
\restrict <key>

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
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;


-- YB: preserve restored reltuples during index creation
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_enable_update_reltuples_after_create_index') THEN
    EXECUTE 'SET yb_enable_update_reltuples_after_create_index TO false';
  END IF;
END $$;
--
-- Name: DATABASE yugabyte; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON DATABASE yugabyte IS 'default administrative connection database';


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: pg_database_owner
--

\unrestrict <key>
\if :use_roles
REVOKE USAGE ON SCHEMA public FROM PUBLIC;
GRANT ALL ON SCHEMA public TO PUBLIC;
\endif
\restrict <key>


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

\unrestrict <key>

--
-- YSQL database cluster dump complete
--

