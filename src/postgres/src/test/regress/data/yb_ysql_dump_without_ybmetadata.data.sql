--
-- YSQL database dump
--

-- Dumped from database version 15.2-YB-2.25.0.0-b0
-- Dumped by ysql_dump version 15.2-YB-2.25.0.0-b0

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

SET default_tablespace = '';

SET default_table_access_method = heap;

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
-- YSQL database dump complete
--

