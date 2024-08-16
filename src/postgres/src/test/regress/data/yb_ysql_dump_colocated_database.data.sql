--
-- YSQL database dump
--

-- Dumped from database version 15.2-YB-2.23.1.1505-b0
-- Dumped by ysql_dump version 15.2-YB-2.23.1.1505-b0

SET yb_binary_restore = true;
SET yb_ignore_heap_pg_class_oids = false;
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

\if :use_tablespaces
    SET default_tablespace = '';
\endif

--
-- Name: htest; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16412'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16411'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16410'::pg_catalog.oid);


-- For YB colocation backup, must preserve implicit tablegroup pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16387'::pg_catalog.oid);
CREATE TABLE public.htest (
    k1 integer,
    k2 text,
    k3 integer,
    v1 integer,
    v2 text
)
PARTITION BY HASH (k1)
WITH (colocation_id='123456');


\if :use_roles
    ALTER TABLE public.htest OWNER TO yugabyte_test;
\endif

SET default_table_access_method = heap;

--
-- Name: htest_1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16415'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16414'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16413'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16413'::pg_catalog.oid);

CREATE TABLE public.htest_1 (
    k1 integer,
    k2 text,
    k3 integer,
    v1 integer,
    v2 text
)
WITH (colocation_id='234567');


\if :use_roles
    ALTER TABLE public.htest_1 OWNER TO yugabyte_test;
\endif

--
-- Name: tbl; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16385'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16384'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16384'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16388'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16388'::pg_catalog.oid);

CREATE TABLE public.tbl (
    k integer NOT NULL,
    v integer,
    CONSTRAINT tbl_pkey PRIMARY KEY(k ASC)
)
WITH (colocation_id='20001');


\if :use_roles
    ALTER TABLE public.tbl OWNER TO yugabyte_test;
\endif

--
-- Name: tbl2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16392'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16391'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16390'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16390'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16393'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16393'::pg_catalog.oid);

CREATE TABLE public.tbl2 (
    k integer NOT NULL,
    v integer,
    v2 text,
    CONSTRAINT tbl2_pkey PRIMARY KEY(k ASC)
)
WITH (colocation_id='20002');


\if :use_roles
    ALTER TABLE public.tbl2 OWNER TO yugabyte_test;
\endif

--
-- Name: tbl3; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16401'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16400'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16399'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16399'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16402'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16402'::pg_catalog.oid);

CREATE TABLE public.tbl3 (
    k integer NOT NULL,
    v integer,
    CONSTRAINT tbl3_pkey PRIMARY KEY((k) HASH)
)
WITH (colocation='false')
SPLIT INTO 3 TABLETS;


\if :use_roles
    ALTER TABLE public.tbl3 OWNER TO yugabyte_test;
\endif

--
-- Name: tbl4; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16407'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16406'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16405'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16405'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16408'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16408'::pg_catalog.oid);

CREATE TABLE public.tbl4 (
    k integer NOT NULL,
    v integer,
    v2 text,
    CONSTRAINT tbl4_pkey PRIMARY KEY((k) HASH)
)
WITH (colocation='false')
SPLIT INTO 3 TABLETS;


\if :use_roles
    ALTER TABLE public.tbl4 OWNER TO yugabyte_test;
\endif

--
-- Name: htest_1; Type: TABLE ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.htest ATTACH PARTITION public.htest_1 FOR VALUES WITH (modulus 2, remainder 0);


--
-- Data for Name: htest_1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.htest_1 (k1, k2, k3, v1, v2) FROM stdin;
\.


--
-- Data for Name: tbl; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl (k, v) FROM stdin;
\.


--
-- Data for Name: tbl2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl2 (k, v, v2) FROM stdin;
\.


--
-- Data for Name: tbl3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl3 (k, v) FROM stdin;
\.


--
-- Data for Name: tbl4; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl4 (k, v, v2) FROM stdin;
\.


--
-- Name: partial_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16398'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16398'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY partial_idx ON public.tbl2 USING lsm (k ASC, v DESC) WITH (colocation_id=40001) WHERE ((k > 10) AND (k < 20) AND (v > 200));


--
-- Name: partial_unique_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16397'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16397'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY partial_unique_idx ON public.tbl USING lsm (v DESC) WITH (colocation_id=40000) WHERE ((v >= 100) AND (v <= 200));


--
-- Name: tbl2_v2_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16396'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16396'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl2_v2_idx ON public.tbl2 USING lsm (v2 ASC) WITH (colocation_id=20004);


--
-- Name: tbl3_v_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16404'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16404'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY tbl3_v_idx ON public.tbl3 USING lsm (v HASH) SPLIT INTO 3 TABLETS;


--
-- Name: tbl_v_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16395'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16395'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY tbl_v_idx ON public.tbl USING lsm (v DESC) WITH (colocation_id=20003);


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

