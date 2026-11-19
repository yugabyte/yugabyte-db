






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





COPY public."CoMPaNy" (id_company, "IBAN", name) FROM stdin;
1991	12345677890	Cyberdyne Systems
\.






COPY public.people (firstname) FROM stdin;
Robert
\.






SELECT pg_catalog.setval('public."CoMPaNy_id_company_seq"', 1, false);






