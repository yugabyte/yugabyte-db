--
-- YSQL database dump
--

-- Dumped from database version 15.2-YB-2.23.1.0-b0
-- Dumped by ysql_dump version 15.2-YB-2.23.1.0-b0

SET yb_binary_restore = true;
SET yb_ignore_pg_class_oids = false;
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

SET default_table_access_method = heap;

--
-- Name: t2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16390'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16389'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16388'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16388'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16391'::pg_catalog.oid);
CREATE TABLE public.t2 (
    col integer
)
WITH (colocation_id='20001');


\if :use_roles
    ALTER TABLE public.t2 OWNER TO yugabyte_test;
\endif

--
-- Name: mv1; Type: MATERIALIZED VIEW; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16417'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16416'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16415'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16415'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16402'::pg_catalog.oid);
CREATE MATERIALIZED VIEW public.mv1
WITH (colocation_id='20003') AS
 SELECT t2.col
   FROM public.t2;


\if :use_roles
    ALTER TABLE public.mv1 OWNER TO yugabyte_test;
\endif

--
-- Name: t1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16385'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16384'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16384'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16387'::pg_catalog.oid);
CREATE TABLE public.t1 (
    col integer
)
WITH (colocation_id='20001');


\if :use_roles
    ALTER TABLE public.t1 OWNER TO yugabyte_test;
\endif

--
-- Name: t3; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16394'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16393'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16392'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16392'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16387'::pg_catalog.oid);
CREATE TABLE public.t3 (
    col integer
)
WITH (colocation_id='20002');


\if :use_roles
    ALTER TABLE public.t3 OWNER TO yugabyte_test;
\endif

--
-- Name: t4; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16397'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16396'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16395'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16395'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16398'::pg_catalog.oid);

-- For YB colocation backup without tablespace information, must preserve default tablegroup tables
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_default(true);
CREATE TABLE public.t4 (
    col integer
)
WITH (colocation_id='20001');


\if :use_roles
    ALTER TABLE public.t4 OWNER TO yugabyte_test;
\endif

--
-- Name: t5; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16401'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16400'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16399'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16399'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16402'::pg_catalog.oid);
CREATE TABLE public.t5 (
    col integer
)
WITH (colocation_id='20001');


\if :use_roles
    ALTER TABLE public.t5 OWNER TO yugabyte_test;
\endif

--
-- Name: t6; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16405'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16404'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16403'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16403'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16391'::pg_catalog.oid);
CREATE TABLE public.t6 (
    col integer
)
WITH (colocation_id='20002');


\if :use_roles
    ALTER TABLE public.t6 OWNER TO yugabyte_test;
\endif

--
-- Name: t7; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16408'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16407'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16406'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16406'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16409'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16409'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16387'::pg_catalog.oid);
CREATE TABLE public.t7 (
    a integer,
    b integer,
    c integer,
    d integer NOT NULL,
    CONSTRAINT t7_pkey PRIMARY KEY(d ASC)
)
WITH (colocation_id='20003');


\if :use_roles
    ALTER TABLE public.t7 OWNER TO yugabyte_test;
\endif

--
-- Data for Name: t1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t1 (col) FROM stdin;
\.


--
-- Data for Name: t2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t2 (col) FROM stdin;
\.


--
-- Data for Name: t3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t3 (col) FROM stdin;
\.


--
-- Data for Name: t4; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t4 (col) FROM stdin;
\.


--
-- Data for Name: t5; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t5 (col) FROM stdin;
\.


--
-- Data for Name: t6; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t6 (col) FROM stdin;
\.


--
-- Data for Name: t7; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.t7 (a, b, c, d) FROM stdin;
\.


--
-- Name: i1; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16411'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16411'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16398'::pg_catalog.oid);

-- For YB colocation backup without tablespace information, must preserve default tablegroup tables
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_default(true);
CREATE INDEX NONCONCURRENTLY i1 ON public.t7 USING lsm (a ASC) WITH (colocation_id=20002);


--
-- Name: i2; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16412'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16412'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16391'::pg_catalog.oid);
CREATE INDEX NONCONCURRENTLY i2 ON public.t7 USING lsm (b ASC) WITH (colocation_id=20003);


--
-- Name: i21; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16414'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16414'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16402'::pg_catalog.oid);
CREATE INDEX NONCONCURRENTLY i21 ON public.t2 USING lsm (col ASC) WITH (colocation_id=20002);


--
-- Name: i3; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16413'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16413'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16391'::pg_catalog.oid);
CREATE INDEX NONCONCURRENTLY i3 ON public.t7 USING lsm (c ASC) WITH (colocation_id=20004);


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
-- Name: mv1; Type: MATERIALIZED VIEW DATA; Schema: public; Owner: yugabyte_test
--

REFRESH MATERIALIZED VIEW public.mv1;


--
-- YSQL database dump complete
--
