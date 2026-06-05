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
-- Name: rename_owner_src_db; Type: DATABASE; Schema: -; Owner: rename_owner_src_role
--

CREATE DATABASE rename_owner_src_db WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8' colocation = false;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_tgt_role') AS role_exists \gset
\if :role_exists
    ALTER DATABASE rename_owner_src_db OWNER TO rename_owner_tgt_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_tgt_role
\endif
\endif

\connect rename_owner_src_db

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
-- Name: fn_other(); Type: FUNCTION; Schema: public; Owner: rename_owner_other_role
--

CREATE FUNCTION public.fn_other() RETURNS integer
    LANGUAGE sql
    AS $$SELECT 2$$;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_other_role') AS role_exists \gset
\if :role_exists
    ALTER FUNCTION public.fn_other() OWNER TO rename_owner_other_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_other_role
\endif
\endif

--
-- Name: fn_src(); Type: FUNCTION; Schema: public; Owner: rename_owner_src_role
--

CREATE FUNCTION public.fn_src() RETURNS integer
    LANGUAGE sql
    AS $$SELECT 1$$;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_tgt_role') AS role_exists \gset
\if :role_exists
    ALTER FUNCTION public.fn_src() OWNER TO rename_owner_tgt_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_tgt_role
\endif
\endif

--
-- Name: seq_other; Type: SEQUENCE; Schema: public; Owner: rename_owner_other_role
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16396'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16396'::pg_catalog.oid);

CREATE SEQUENCE public.seq_other
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_other_role') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.seq_other OWNER TO rename_owner_other_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_other_role
\endif
\endif

--
-- Name: seq_src; Type: SEQUENCE; Schema: public; Owner: rename_owner_src_role
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16389'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16389'::pg_catalog.oid);

CREATE SEQUENCE public.seq_src
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_tgt_role') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.seq_src OWNER TO rename_owner_tgt_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_tgt_role
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = '';
\endif

SET default_table_access_method = heap;

--
-- Name: t_other; Type: TABLE; Schema: public; Owner: rename_owner_other_role
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16393'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16392'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16391'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16391'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16394'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16394'::pg_catalog.oid);

CREATE TABLE public.t_other (
    k integer NOT NULL,
    CONSTRAINT t_other_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;
ALTER TABLE public.t_other SET (yb_presplit='3');


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_other_role') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.t_other OWNER TO rename_owner_other_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_other_role
\endif
\endif

--
-- Name: t_src; Type: TABLE; Schema: public; Owner: rename_owner_src_role
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

CREATE TABLE public.t_src (
    k integer NOT NULL,
    CONSTRAINT t_src_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;
ALTER TABLE public.t_src SET (yb_presplit='3');


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rename_owner_tgt_role') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.t_src OWNER TO rename_owner_tgt_role;
\else
    \echo 'Skipping owner privilege due to missing role:' rename_owner_tgt_role
\endif
\endif

--
-- Data for Name: t_other; Type: TABLE DATA; Schema: public; Owner: rename_owner_other_role
--

COPY public.t_other (k) FROM stdin;
\.


--
-- Data for Name: t_src; Type: TABLE DATA; Schema: public; Owner: rename_owner_src_role
--

COPY public.t_src (k) FROM stdin;
\.


--
-- Name: seq_other; Type: SEQUENCE SET; Schema: public; Owner: rename_owner_other_role
--

SELECT pg_catalog.setval('public.seq_other', 1, false);


--
-- Name: seq_src; Type: SEQUENCE SET; Schema: public; Owner: rename_owner_src_role
--

SELECT pg_catalog.setval('public.seq_src', 1, false);


--
-- Statistics for Name: t_other; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_other',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: t_src; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_src',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


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
-- Statistics for Name: t_other_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_other_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: t_src_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 't_src_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- YSQL database dump complete
--

