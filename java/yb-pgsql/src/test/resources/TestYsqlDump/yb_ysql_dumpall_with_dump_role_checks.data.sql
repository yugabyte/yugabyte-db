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
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'User_"_WITH_""_different''_''quotes'' and spaces') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' "User_""_WITH_""""_different'_'quotes' and spaces"
\else
    CREATE ROLE "User_""_WITH_""""_different'_'quotes' and spaces";
    ALTER ROLE "User_""_WITH_""""_different'_'quotes' and spaces" WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
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
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user7') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' regress_priv_user7
\else
    CREATE ROLE regress_priv_user7;
    ALTER ROLE regress_priv_user7 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS;
\endif

\set role_exists false
\if :ignore_existing_roles
    SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user8') AS role_exists \gset
\endif
\if :role_exists
    \echo 'Role already exists:' regress_priv_user8
\else
    CREATE ROLE regress_priv_user8;
    ALTER ROLE regress_priv_user8 WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB LOGIN NOREPLICATION NOBYPASSRLS;
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
-- User Config "regress_priv_user7"
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user7') AS role_exists \gset
\if :role_exists
    ALTER ROLE regress_priv_user7 SET log_min_messages TO 'LOG';
\else
    \echo 'Skipping alter role due to missing role:' regress_priv_user7
\endif



--
-- Role memberships
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user8') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    GRANT pg_read_all_settings TO regress_priv_user8 WITH ADMIN OPTION GRANTED BY yugabyte_test;
\else
    \echo 'Skipping grant privilege due to missing role:' regress_priv_user8 'OR' yugabyte_test
\endif

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user7') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    GRANT pg_write_all_data TO regress_priv_user7 GRANTED BY yugabyte_test;
\else
    \echo 'Skipping grant privilege due to missing role:' regress_priv_user7 'OR' yugabyte_test
\endif




--
-- YB Profiles
--

CREATE PROFILE profile_3_failed LIMIT FAILED_LOGIN_ATTEMPTS 3;


--
-- YB Role-Profile Mappings
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user7') AS role_exists \gset
\if :role_exists
    ALTER ROLE regress_priv_user7 PROFILE profile_3_failed;
    UPDATE pg_catalog.pg_yb_role_profile
    SET rolprfstatus = 'o',
        rolprffailedloginattempts = 0
    WHERE rolprfrole = (SELECT oid FROM pg_authid WHERE rolname = 'regress_priv_user7')
      AND rolprfprofile = (SELECT oid FROM pg_yb_profile WHERE prfname = 'profile_3_failed');
\else
    \echo 'Skipping alter role due to missing role:' regress_priv_user7
\endif


--
-- Tablespaces
--

-- Set variable ignore_existing_tablespaces (if not already set)
\if :{?ignore_existing_tablespaces}
\else
\set ignore_existing_tablespaces false
\endif

\set tablespace_exists false
\if :ignore_existing_tablespaces
    SELECT EXISTS(SELECT 1 FROM pg_tablespace WHERE spcname = 'tsp1') AS tablespace_exists \gset
\endif
\if :tablespace_exists
    \echo 'Tablespace tsp1 already exists.'
\else
    CREATE TABLESPACE tsp1 OWNER yugabyte_test LOCATION '';
\endif

\set tablespace_exists false
\if :ignore_existing_tablespaces
    SELECT EXISTS(SELECT 1 FROM pg_tablespace WHERE spcname = 'tsp2') AS tablespace_exists \gset
\endif
\if :tablespace_exists
    \echo 'Tablespace tsp2 already exists.'
\else
    CREATE TABLESPACE tsp2 OWNER yugabyte_test LOCATION '' WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]}');
\endif

\set tablespace_exists false
\if :ignore_existing_tablespaces
    SELECT EXISTS(SELECT 1 FROM pg_tablespace WHERE spcname = 'tsp_unused') AS tablespace_exists \gset
\endif
\if :tablespace_exists
    \echo 'Tablespace tsp_unused already exists.'
\else
    CREATE TABLESPACE tsp_unused OWNER yugabyte_test LOCATION '' WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"dc_unused","zone":"z_unused","min_num_replicas":1}]}');
\endif



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

-- Dumped from database version 15.2-YB-2.25.0.0-b0
-- Dumped by ysql_dump version 15.2-YB-2.25.0.0-b0

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

-- Dumped from database version 15.12-YB-2.25.2.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.25.2.0-b0

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

-- Dumped from database version 15.12-YB-2.25.2.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.25.2.0-b0

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
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'postgres') AS role_exists \gset
\if :role_exists
    ALTER DATABASE system_platform OWNER TO postgres;
\else
    \echo 'Skipping owner privilege due to missing role:' postgres
\endif
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

-- Dumped from database version 15.12-YB-2.25.2.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.25.2.0-b0

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
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'postgres') AS role_exists \gset
\if :role_exists
    ALTER DATABASE yugabyte OWNER TO postgres;
\else
    \echo 'Skipping owner privilege due to missing role:' postgres
\endif
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
-- Name: yugabyte; Type: DATABASE PROPERTIES; Schema: -; Owner: postgres
--

\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_priv_user8') AS role_exists \gset
\if :role_exists
    ALTER ROLE regress_priv_user8 IN DATABASE yugabyte SET log_min_messages TO 'LOG';
\else
    \echo 'Skipping alter role due to missing role:' regress_priv_user8
\endif

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

\if :use_tablespaces
    SET default_tablespace = tsp1;
\endif

--
-- Name: grp_with_spc; Type: TABLEGROUP; Schema: -; Owner: yugabyte_test; Tablespace: tsp1
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16393'::pg_catalog.oid);
CREATE TABLEGROUP grp_with_spc;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLEGROUP grp_with_spc OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = '';
\endif

--
-- Name: grp_without_spc; Type: TABLEGROUP; Schema: -; Owner: yugabyte_test
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16392'::pg_catalog.oid);
CREATE TABLEGROUP grp_without_spc;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLEGROUP grp_without_spc OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = tsp1;
\endif

SET default_table_access_method = heap;

--
-- Name: table1; Type: TABLE; Schema: public; Owner: yugabyte_test; Tablespace: tsp1
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16385'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16384'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16384'::pg_catalog.oid);

CREATE TABLE public.table1 (
    id integer
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.table1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = tsp2;
\endif

--
-- Name: table2; Type: TABLE; Schema: public; Owner: yugabyte_test; Tablespace: tsp2
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16390'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16389'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16388'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16388'::pg_catalog.oid);

CREATE TABLE public.table2 (
    name character varying
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.table2 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = '';
\endif

--
-- Name: tbl_with_grp_with_spc; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16396'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16395'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16394'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16394'::pg_catalog.oid);

CREATE TABLE public.tbl_with_grp_with_spc (
    a integer
)
WITH (autovacuum_enabled='true', colocation_id='20001')
TABLEGROUP grp_with_spc;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl_with_grp_with_spc OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Data for Name: table1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.table1 (id) FROM stdin;
\.


--
-- Data for Name: table2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.table2 (name) FROM stdin;
\.


--
-- Data for Name: tbl_with_grp_with_spc; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl_with_grp_with_spc (a) FROM stdin;
\.


--
-- Statistics for Name: table1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'table1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: table2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'table2',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl_with_grp_with_spc; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl_with_grp_with_spc',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


\if :use_tablespaces
    SET default_tablespace = tsp2;
\endif

--
-- Name: idx1; Type: INDEX; Schema: public; Owner: yugabyte_test; Tablespace: tsp2
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16387'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16387'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY idx1 ON public.table1 USING lsm (id HASH) SPLIT INTO 3 TABLETS;


\if :use_tablespaces
    SET default_tablespace = tsp1;
\endif

--
-- Name: idx2; Type: INDEX; Schema: public; Owner: yugabyte_test; Tablespace: tsp1
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16391'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16391'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY idx2 ON public.table2 USING lsm (name HASH) SPLIT INTO 3 TABLETS;


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
-- Statistics for Name: idx1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'idx1',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: idx2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'idx2',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- YSQL database dump complete
--

--
-- YSQL database cluster dump complete
--

