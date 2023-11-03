--
-- YSQL database cluster dump
--

SET default_transaction_read_only = off;

SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;

--
-- Roles
--

CREATE ROLE postgres;
ALTER ROLE postgres WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS;
CREATE ROLE yb_db_admin;
ALTER ROLE yb_db_admin WITH NOSUPERUSER NOINHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
CREATE ROLE yb_extension;
ALTER ROLE yb_extension WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
CREATE ROLE yb_fdw;
ALTER ROLE yb_fdw WITH NOSUPERUSER INHERIT NOCREATEROLE NOCREATEDB NOLOGIN NOREPLICATION NOBYPASSRLS;
CREATE ROLE yugabyte;
ALTER ROLE yugabyte WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS PASSWORD 'md52c2dc7d65d3e364f08b8addff5a54bf5';
CREATE ROLE yugabyte_test;
ALTER ROLE yugabyte_test WITH SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN NOREPLICATION BYPASSRLS;




--
-- Tablespaces
--

CREATE TABLESPACE tsp1 OWNER yugabyte_test LOCATION '';
CREATE TABLESPACE tsp2 OWNER yugabyte_test LOCATION '' WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]}');
CREATE TABLESPACE tsp_unused OWNER yugabyte_test LOCATION '' WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"dc_unused","zone":"z_unused","min_num_replicas":1}]}');


\connect template1

--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.21.0.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.21.0.0-b0

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: FUNCTION pg_stat_statements_reset(); Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset() FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- YSQL database dump complete
--

\connect postgres

--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.21.0.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.21.0.0-b0

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: FUNCTION pg_stat_statements_reset(); Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset() FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- YSQL database dump complete
--

--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.21.0.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.21.0.0-b0

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: system_platform; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE system_platform WITH TEMPLATE = template0 ENCODING = 'UTF8' LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8';


ALTER DATABASE system_platform OWNER TO postgres;

\connect system_platform

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: DATABASE system_platform; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON DATABASE system_platform IS 'system database for YugaByte platform';


--
-- Name: FUNCTION pg_stat_statements_reset(); Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset() FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- YSQL database dump complete
--

--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.21.0.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.21.0.0-b0

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: yugabyte; Type: DATABASE; Schema: -; Owner: postgres
--

CREATE DATABASE yugabyte WITH TEMPLATE = template0 ENCODING = 'UTF8' LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8';


ALTER DATABASE yugabyte OWNER TO postgres;

\connect yugabyte

SET yb_binary_restore = true;
SET yb_non_ddl_txn_for_sys_tables_allowed = true;
SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: DATABASE yugabyte; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON DATABASE yugabyte IS 'default administrative connection database';


SET default_tablespace = tsp1;

--
-- Name: grp_with_spc; Type: TABLEGROUP; Schema: -; Owner: yugabyte_test; Tablespace: tsp1
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16393'::pg_catalog.oid);
CREATE TABLEGROUP grp_with_spc;


ALTER TABLEGROUP grp_with_spc OWNER TO yugabyte_test;

SET default_tablespace = '';

--
-- Name: grp_without_spc; Type: TABLEGROUP; Schema: -; Owner: yugabyte_test
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16392'::pg_catalog.oid);
CREATE TABLEGROUP grp_without_spc;


ALTER TABLEGROUP grp_without_spc OWNER TO yugabyte_test;

SET default_tablespace = tsp1;

SET default_with_oids = false;

--
-- Name: table1; Type: TABLE; Schema: public; Owner: yugabyte_test; Tablespace: tsp1
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16385'::pg_catalog.oid);

CREATE TABLE public.table1 (
    id integer
)
SPLIT INTO 3 TABLETS;


ALTER TABLE public.table1 OWNER TO yugabyte_test;

SET default_tablespace = tsp2;

--
-- Name: table2; Type: TABLE; Schema: public; Owner: yugabyte_test; Tablespace: tsp2
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16390'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16389'::pg_catalog.oid);

CREATE TABLE public.table2 (
    name character varying
)
SPLIT INTO 3 TABLETS;


ALTER TABLE public.table2 OWNER TO yugabyte_test;

SET default_tablespace = '';

--
-- Name: tbl_with_grp_with_spc; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16396'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16395'::pg_catalog.oid);

CREATE TABLE public.tbl_with_grp_with_spc (
    a integer
)
WITH (autovacuum_enabled='true', colocation_id='20001')
TABLEGROUP grp_with_spc;


ALTER TABLE public.tbl_with_grp_with_spc OWNER TO yugabyte_test;

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


SET default_tablespace = tsp2;

--
-- Name: idx1; Type: INDEX; Schema: public; Owner: yugabyte_test; Tablespace: tsp2
--

CREATE INDEX NONCONCURRENTLY idx1 ON public.table1 USING lsm (id HASH) SPLIT INTO 3 TABLETS;


SET default_tablespace = tsp1;

--
-- Name: idx2; Type: INDEX; Schema: public; Owner: yugabyte_test; Tablespace: tsp1
--

CREATE INDEX NONCONCURRENTLY idx2 ON public.table2 USING lsm (name HASH) SPLIT INTO 3 TABLETS;


--
-- Name: FUNCTION pg_stat_statements_reset(); Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset() FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- Name: TABLE pg_stat_statements; Type: ACL; Schema: pg_catalog; Owner: postgres
--

SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE pg_catalog.pg_stat_statements TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);


--
-- YSQL database dump complete
--

--
-- YSQL database cluster dump complete
--

