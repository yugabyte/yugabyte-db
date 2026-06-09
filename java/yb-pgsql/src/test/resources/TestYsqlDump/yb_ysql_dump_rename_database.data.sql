--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.31.0.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.31.0.0-b0

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
-- Name: rename_db_tgt; Type: DATABASE; Schema: -; Owner: yugabyte_test
--

CREATE DATABASE rename_db_tgt WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\if :use_roles
    ALTER DATABASE rename_db_tgt OWNER TO yugabyte_test;
\endif

\connect rename_db_tgt

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


-- YB: preserve restored reltuples during index creation
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_enable_update_reltuples_after_create_index') THEN
    EXECUTE 'SET yb_enable_update_reltuples_after_create_index TO false';
  END IF;
END $$;
--
-- Name: DATABASE rename_db_tgt; Type: COMMENT; Schema: -; Owner: yugabyte_test
--

COMMENT ON DATABASE rename_db_tgt IS 'rename_database test source';


--
-- Name: rename_db_tgt; Type: DATABASE PROPERTIES; Schema: -; Owner: yugabyte_test
--

ALTER DATABASE rename_db_tgt SET "TimeZone" TO 'UTC';
\if :use_roles
ALTER ROLE yugabyte_test IN DATABASE rename_db_tgt SET search_path TO 'public';
\endif


\connect rename_db_tgt

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


-- YB: preserve restored reltuples during index creation
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_enable_update_reltuples_after_create_index') THEN
    EXECUTE 'SET yb_enable_update_reltuples_after_create_index TO false';
  END IF;
END $$;
--
-- Name: s; Type: SEQUENCE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16390'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16390'::pg_catalog.oid);

CREATE SEQUENCE public.s
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
    ALTER TABLE public.s OWNER TO yugabyte_test;
\endif

\if :use_tablespaces
    SET default_tablespace = '';
\endif

SET default_table_access_method = heap;

--
-- Name: t; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16385'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16384'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16384'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16387'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16387'::pg_catalog.oid);

CREATE TABLE public.t (
    k integer NOT NULL,
    v text,
    CONSTRAINT t_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;
ALTER TABLE public.t SET (yb_presplit='3');


\if :use_roles
    ALTER TABLE public.t OWNER TO yugabyte_test;
\endif

--
-- Name: vw; Type: VIEW; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16393'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16392'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16391'::pg_catalog.oid);

CREATE VIEW public.vw AS
 SELECT t.k,
    t.v
   FROM public.t;


\if :use_roles
    ALTER TABLE public.vw OWNER TO yugabyte_test;
\endif

--
-- Data for Name: t; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t (k, v) FROM stdin;
1	a
2	b
\.


--
-- Name: s; Type: SEQUENCE SET; Schema: public; Owner: yugabyte_test
--

SELECT pg_catalog.setval('public.s', 1, false);


--
-- Statistics for Name: t; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Name: t_v_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16389'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16389'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY t_v_idx ON public.t USING lsm (v HASH) SPLIT INTO 3 TABLETS;
ALTER INDEX public.t_v_idx SET (yb_presplit='3');


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
-- Statistics for Name: t_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: t_v_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_v_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- YSQL database dump complete
--

