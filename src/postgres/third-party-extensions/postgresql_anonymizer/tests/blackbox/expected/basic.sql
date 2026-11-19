






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

SET default_tablespace = '';

SET default_table_access_method = heap;





CREATE TABLE public."CoMPaNy" (
    id_company integer NOT NULL,
    "IBAN" text,
    name text
);


ALTER TABLE public."CoMPaNy" OWNER TO postgres;





CREATE SEQUENCE public."CoMPaNy_id_company_seq"
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public."CoMPaNy_id_company_seq" OWNER TO postgres;





ALTER SEQUENCE public."CoMPaNy_id_company_seq" OWNED BY public."CoMPaNy".id_company;






CREATE TABLE public.people (
    firstname text
);


ALTER TABLE public.people OWNER TO postgres;





ALTER TABLE ONLY public."CoMPaNy" ALTER COLUMN id_company SET DEFAULT nextval('public."CoMPaNy_id_company_seq"'::regclass);






COPY public."CoMPaNy" (id_company, "IBAN", name) FROM stdin;
1991	12345677890	Cyberdyne Systems
\.






COPY public.people (firstname) FROM stdin;
Robert
\.






SELECT pg_catalog.setval('public."CoMPaNy_id_company_seq"', 1, false);






ALTER TABLE ONLY public."CoMPaNy"
    ADD CONSTRAINT "CoMPaNy_id_company_key" UNIQUE (id_company);






