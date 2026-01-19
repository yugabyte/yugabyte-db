--
-- YSQL database dump
--

-- Dumped from database version 15.2-YB-2.25.2.0-b0
-- Dumped by ysql_dump version 15.2-YB-2.25.2.0-b0

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
-- Name: overflow; Type: TYPE; Schema: public; Owner: yugabyte_test
--

CREATE TYPE public.overflow AS ENUM (
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
    'I',
    'J',
    'K',
    'L',
    'M',
    'N',
    'O',
    'P',
    'Q',
    'R',
    'S',
    'T',
    'U',
    'V',
    'W',
    'X',
    'Z'
);


ALTER TYPE public.overflow OWNER TO yugabyte_test;

--
-- Name: underflow; Type: TYPE; Schema: public; Owner: yugabyte_test
--

CREATE TYPE public.underflow AS ENUM (
    'A',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
    'I',
    'J',
    'K',
    'L',
    'M',
    'N',
    'O',
    'P',
    'Q',
    'R',
    'S',
    'T',
    'U',
    'V',
    'W',
    'X',
    'Y',
    'Z'
);


ALTER TYPE public.underflow OWNER TO yugabyte_test;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: level0; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.level0 (
    c1 integer,
    c2 text NOT NULL,
    c3 text,
    c4 text,
    CONSTRAINT level0_c1_cons CHECK ((c1 > 0)),
    CONSTRAINT level0_c1_cons2 CHECK ((c1 IS NULL)) NO INHERIT
);


ALTER TABLE public.level0 OWNER TO yugabyte_test;

--
-- Name: level1_0; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.level1_0 (
    c1 integer NOT NULL,
    CONSTRAINT level1_0_pkey PRIMARY KEY(c1 ASC)
)
INHERITS (public.level0);


ALTER TABLE public.level1_0 OWNER TO yugabyte_test;

--
-- Name: level1_1; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.level1_1 (
    c2 text,
    CONSTRAINT level1_1_c1_cons CHECK ((c1 >= 2)),
    CONSTRAINT level1_1_pkey PRIMARY KEY((c2) HASH)
)
INHERITS (public.level0);


ALTER TABLE public.level1_1 OWNER TO yugabyte_test;

--
-- Name: level2_0; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.level2_0 (
    c1 integer,
    c2 text,
    c3 text NOT NULL
)
INHERITS (public.level1_0);


ALTER TABLE public.level2_0 OWNER TO yugabyte_test;

--
-- Name: level2_1; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.level2_1 (
    c1 integer,
    c2 text,
    c3 text NOT NULL,
    c4 text NOT NULL,
    CONSTRAINT level2_1_pkey PRIMARY KEY((c4) HASH)
)
INHERITS (public.level1_0, public.level1_1);


ALTER TABLE public.level2_1 OWNER TO yugabyte_test;

--
-- Name: p1; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.p1 (
    a integer,
    b integer,
    c integer
);


ALTER TABLE public.p1 OWNER TO yugabyte_test;

--
-- Name: p2; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.p2 (
    a integer,
    b integer,
    c integer
);


ALTER TABLE public.p2 OWNER TO yugabyte_test;

--
-- Name: p3; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.p3 (
    a integer,
    b integer,
    c integer
);


ALTER TABLE public.p3 OWNER TO yugabyte_test;

--
-- Name: part_uniq_const; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.part_uniq_const (
    v1 integer,
    v2 integer
)
PARTITION BY RANGE (v1);


ALTER TABLE public.part_uniq_const OWNER TO yugabyte_test;

--
-- Name: part_uniq_const_30_50; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.part_uniq_const_30_50 (
    v1 integer,
    v2 integer
);


ALTER TABLE public.part_uniq_const_30_50 OWNER TO yugabyte_test;

--
-- Name: part_uniq_const_50_100; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.part_uniq_const_50_100 (
    v1 integer,
    v2 integer
);


ALTER TABLE public.part_uniq_const_50_100 OWNER TO yugabyte_test;

--
-- Name: part_uniq_const_default; Type: TABLE; Schema: public; Owner: yugabyte_test
--

CREATE TABLE public.part_uniq_const_default (
    v1 integer,
    v2 integer
);


ALTER TABLE public.part_uniq_const_default OWNER TO yugabyte_test;

--
-- Name: part_uniq_const_30_50; Type: TABLE ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const ATTACH PARTITION public.part_uniq_const_30_50 FOR VALUES FROM (30) TO (50);


--
-- Name: part_uniq_const_50_100; Type: TABLE ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const ATTACH PARTITION public.part_uniq_const_50_100 FOR VALUES FROM (50) TO (100);


--
-- Name: part_uniq_const_default; Type: TABLE ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const ATTACH PARTITION public.part_uniq_const_default DEFAULT;


--
-- Data for Name: level0; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.level0 (c1, c2, c3, c4) FROM stdin;
\N	0	\N	\N
\.


--
-- Data for Name: level1_0; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.level1_0 (c1, c2, c3, c4) FROM stdin;
2	1_0	1_0	\N
\.


--
-- Data for Name: level1_1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.level1_1 (c1, c2, c3, c4) FROM stdin;
\N	1_1	\N	1_1
\.


--
-- Data for Name: level2_0; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.level2_0 (c1, c2, c3, c4) FROM stdin;
1	2_0	2_0	\N
\.


--
-- Data for Name: level2_1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.level2_1 (c1, c2, c3, c4) FROM stdin;
2	2_1	2_1	2_1
\.


--
-- Data for Name: p1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.p1 (a, b, c) FROM stdin;
\.


--
-- Data for Name: p2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.p2 (a, b, c) FROM stdin;
\.


--
-- Data for Name: p3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.p3 (a, b, c) FROM stdin;
\.


--
-- Data for Name: part_uniq_const_30_50; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_30_50 (v1, v2) FROM stdin;
31	200
\.


--
-- Data for Name: part_uniq_const_50_100; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_50_100 (v1, v2) FROM stdin;
51	100
\.


--
-- Data for Name: part_uniq_const_default; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_default (v1, v2) FROM stdin;
1	1000
\.


--
-- Statistics for Name: level0; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level0',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level1_0; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level1_0',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level1_1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level1_1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level2_0; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level2_0',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level2_1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level2_1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: p1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'p1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: p2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'p2',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: p3; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'p3',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_30_50; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_30_50',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_50_100; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_50_100',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_default; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_default',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Name: p1 c1; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX NONCONCURRENTLY c1 ON public.p1 USING lsm (a HASH, b ASC);

ALTER TABLE ONLY public.p1
    ADD CONSTRAINT c1 UNIQUE USING INDEX c1;


--
-- Name: p2 c2; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX NONCONCURRENTLY c2 ON public.p2 USING lsm (a ASC, b ASC);

ALTER TABLE ONLY public.p2
    ADD CONSTRAINT c2 UNIQUE USING INDEX c2;


--
-- Name: p3 c3; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX NONCONCURRENTLY c3 ON public.p3 USING lsm ((a, b) HASH, c ASC);

ALTER TABLE ONLY public.p3
    ADD CONSTRAINT c3 UNIQUE USING INDEX c3;


--
-- Name: level1_1_c3_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--

CREATE INDEX level1_1_c3_idx ON public.level1_1 USING lsm (c3 DESC);


--
-- Name: level2_1_c3_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--

CREATE INDEX level2_1_c3_idx ON public.level2_1 USING lsm (c3 ASC);


--
-- Name: part_uniq_const part_uniq_const_unique; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const
    ADD CONSTRAINT part_uniq_const_unique UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_30_50 part_uniq_const_30_50_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const_30_50
    ADD CONSTRAINT part_uniq_const_30_50_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_50_100 part_uniq_const_50_100_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const_50_100
    ADD CONSTRAINT part_uniq_const_50_100_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_50_100 part_uniq_const_50_100_v2_uniq; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

CREATE UNIQUE INDEX NONCONCURRENTLY part_uniq_const_50_100_v2_uniq ON public.part_uniq_const_50_100 USING lsm (v2 ASC);

ALTER TABLE ONLY public.part_uniq_const_50_100
    ADD CONSTRAINT part_uniq_const_50_100_v2_uniq UNIQUE USING INDEX part_uniq_const_50_100_v2_uniq;


--
-- Name: part_uniq_const_default part_uniq_const_default_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.part_uniq_const_default
    ADD CONSTRAINT part_uniq_const_default_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_30_50_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_30_50_v1_v2_key;


--
-- Name: part_uniq_const_50_100_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_50_100_v1_v2_key;


--
-- Name: part_uniq_const_default_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_default_v1_v2_key;


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: pg_database_owner
--

REVOKE USAGE ON SCHEMA public FROM PUBLIC;
GRANT ALL ON SCHEMA public TO PUBLIC;


--
-- Statistics for Name: c1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'c1',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: c2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'c2',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: c3; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'c3',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level1_0_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level1_0_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level1_1_c3_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level1_1_c3_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level1_1_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level1_1_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level2_1_c3_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level2_1_c3_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: level2_1_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'level2_1_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_30_50_v1_v2_key; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_30_50_v1_v2_key',
	'relpages', '0'::integer,
	'reltuples', '1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_50_100_v1_v2_key; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_50_100_v1_v2_key',
	'relpages', '0'::integer,
	'reltuples', '1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_50_100_v2_uniq; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_50_100_v2_uniq',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_default_v1_v2_key; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_default_v1_v2_key',
	'relpages', '0'::integer,
	'reltuples', '1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_unique; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_unique',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- YSQL database dump complete
--

