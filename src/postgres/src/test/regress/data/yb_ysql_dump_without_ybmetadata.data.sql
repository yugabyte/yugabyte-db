--
-- YSQL database dump
--

-- Dumped from database version 11.2-YB-2.15.1.0-b0
-- Dumped by ysql_dump version 11.2-YB-2.15.1.0-b0

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

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
-- YSQL database dump complete
--
