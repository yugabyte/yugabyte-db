--
-- YSQL database dump
--

-- Dumped from database version 15.12-YB-2.25.2.0-b0
-- Dumped by ysql_dump version 15.12-YB-2.25.2.0-b0

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

-- YB: disable auto analyze to avoid conflicts with catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO on', current_database());
  END IF;
END $$;

--
-- Name: hint_plan; Type: SCHEMA; Schema: -; Owner: yugabyte_test
--

CREATE SCHEMA hint_plan;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER SCHEMA hint_plan OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: pg_hint_plan; Type: EXTENSION; Schema: -; Owner: -
--

-- For binary upgrade, create an empty extension and insert objects into it
DROP EXTENSION IF EXISTS pg_hint_plan;
SELECT pg_catalog.binary_upgrade_create_empty_extension('pg_hint_plan', 'hint_plan', false, '1.5.1-yb-1.0', '{16546,16545}', '{"",""}', ARRAY[]::pg_catalog.text[]);


--
-- Name: overflow; Type: TYPE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16661'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16660'::pg_catalog.oid);

CREATE TYPE public.overflow AS ENUM (
);

-- For binary upgrade, must preserve pg_enum oids and sortorders
SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16662'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1'::real);
ALTER TYPE public.overflow ADD VALUE 'A';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16665'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.5'::real);
ALTER TYPE public.overflow ADD VALUE 'B';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16667'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.75'::real);
ALTER TYPE public.overflow ADD VALUE 'C';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16669'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.875'::real);
ALTER TYPE public.overflow ADD VALUE 'D';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16671'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.9375'::real);
ALTER TYPE public.overflow ADD VALUE 'E';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16673'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.96875'::real);
ALTER TYPE public.overflow ADD VALUE 'F';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16675'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.984375'::real);
ALTER TYPE public.overflow ADD VALUE 'G';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16677'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.9921875'::real);
ALTER TYPE public.overflow ADD VALUE 'H';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16679'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99609375'::real);
ALTER TYPE public.overflow ADD VALUE 'I';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16681'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.998046875'::real);
ALTER TYPE public.overflow ADD VALUE 'J';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16683'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.9990234375'::real);
ALTER TYPE public.overflow ADD VALUE 'K';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16685'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99951171875'::real);
ALTER TYPE public.overflow ADD VALUE 'L';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16687'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.999755859375'::real);
ALTER TYPE public.overflow ADD VALUE 'M';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16689'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.9998779296875'::real);
ALTER TYPE public.overflow ADD VALUE 'N';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16691'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99993896484375'::real);
ALTER TYPE public.overflow ADD VALUE 'O';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16693'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99996948242188'::real);
ALTER TYPE public.overflow ADD VALUE 'P';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16695'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99998474121094'::real);
ALTER TYPE public.overflow ADD VALUE 'Q';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16697'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999237060547'::real);
ALTER TYPE public.overflow ADD VALUE 'R';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16699'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999618530273'::real);
ALTER TYPE public.overflow ADD VALUE 'S';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16701'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999809265137'::real);
ALTER TYPE public.overflow ADD VALUE 'T';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16703'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999904632568'::real);
ALTER TYPE public.overflow ADD VALUE 'U';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16705'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999952316284'::real);
ALTER TYPE public.overflow ADD VALUE 'V';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16707'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999976158142'::real);
ALTER TYPE public.overflow ADD VALUE 'W';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16709'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.99999988079071'::real);
ALTER TYPE public.overflow ADD VALUE 'X';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16664'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('2'::real);
ALTER TYPE public.overflow ADD VALUE 'Z';



\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TYPE public.overflow OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: underflow; Type: TYPE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16711'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16710'::pg_catalog.oid);

CREATE TYPE public.underflow AS ENUM (
);

-- For binary upgrade, must preserve pg_enum oids and sortorders
SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16712'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1'::real);
ALTER TYPE public.underflow ADD VALUE 'A';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16759'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000011920929'::real);
ALTER TYPE public.underflow ADD VALUE 'C';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16757'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000023841858'::real);
ALTER TYPE public.underflow ADD VALUE 'D';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16755'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000047683716'::real);
ALTER TYPE public.underflow ADD VALUE 'E';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16753'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000095367432'::real);
ALTER TYPE public.underflow ADD VALUE 'F';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16751'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000190734863'::real);
ALTER TYPE public.underflow ADD VALUE 'G';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16749'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000381469727'::real);
ALTER TYPE public.underflow ADD VALUE 'H';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16747'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00000762939453'::real);
ALTER TYPE public.underflow ADD VALUE 'I';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16745'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00001525878906'::real);
ALTER TYPE public.underflow ADD VALUE 'J';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16743'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00003051757812'::real);
ALTER TYPE public.underflow ADD VALUE 'K';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16741'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00006103515625'::real);
ALTER TYPE public.underflow ADD VALUE 'L';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16739'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.0001220703125'::real);
ALTER TYPE public.underflow ADD VALUE 'M';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16737'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.000244140625'::real);
ALTER TYPE public.underflow ADD VALUE 'N';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16735'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00048828125'::real);
ALTER TYPE public.underflow ADD VALUE 'O';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16733'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.0009765625'::real);
ALTER TYPE public.underflow ADD VALUE 'P';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16731'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.001953125'::real);
ALTER TYPE public.underflow ADD VALUE 'Q';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16729'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.00390625'::real);
ALTER TYPE public.underflow ADD VALUE 'R';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16727'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.0078125'::real);
ALTER TYPE public.underflow ADD VALUE 'S';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16725'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.015625'::real);
ALTER TYPE public.underflow ADD VALUE 'T';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16723'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.03125'::real);
ALTER TYPE public.underflow ADD VALUE 'U';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16721'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.0625'::real);
ALTER TYPE public.underflow ADD VALUE 'V';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16719'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.125'::real);
ALTER TYPE public.underflow ADD VALUE 'W';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16717'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.25'::real);
ALTER TYPE public.underflow ADD VALUE 'X';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16715'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1.5'::real);
ALTER TYPE public.underflow ADD VALUE 'Y';

SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('16714'::pg_catalog.oid);
SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('2'::real);
ALTER TYPE public.underflow ADD VALUE 'Z';



\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TYPE public.underflow OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: yb_cache_invalidate(); Type: FUNCTION; Schema: hint_plan; Owner: yugabyte_test
--

CREATE FUNCTION hint_plan.yb_cache_invalidate() RETURNS trigger
    LANGUAGE c
    AS '$libdir/pg_hint_plan', 'yb_hint_plan_cache_invalidate';

-- For binary upgrade, handle extension membership the hard way
ALTER EXTENSION pg_hint_plan ADD FUNCTION hint_plan.yb_cache_invalidate();


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER FUNCTION hint_plan.yb_cache_invalidate() OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: FUNCTION yb_cache_invalidate(); Type: COMMENT; Schema: hint_plan; Owner: yugabyte_test
--

COMMENT ON FUNCTION hint_plan.yb_cache_invalidate() IS 'invalidate hint plan cache';


\if :use_tablespaces
    SET default_tablespace = '';
\endif

--
-- Name: grp1; Type: TABLEGROUP; Schema: -; Owner: tablegroup_test_user
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16478'::pg_catalog.oid);
CREATE TABLEGROUP grp1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLEGROUP grp1 OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: grp2; Type: TABLEGROUP; Schema: -; Owner: tablegroup_test_user
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16479'::pg_catalog.oid);
CREATE TABLEGROUP grp2;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLEGROUP grp2 OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = tsp1;
\endif

--
-- Name: grp_with_spc; Type: TABLEGROUP; Schema: -; Owner: tablegroup_test_user; Tablespace: tsp1
--


-- For YB tablegroup backup, must preserve pg_yb_tablegroup oid
SELECT pg_catalog.binary_upgrade_set_next_tablegroup_oid('16480'::pg_catalog.oid);
CREATE TABLEGROUP grp_with_spc;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLEGROUP grp_with_spc OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

\if :use_tablespaces
    SET default_tablespace = '';
\endif

SET default_table_access_method = heap;

--
-- Name: hints; Type: TABLE; Schema: hint_plan; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16548'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16547'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16546'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16546'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16550'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16550'::pg_catalog.oid);

CREATE TABLE hint_plan.hints (
    id integer NOT NULL,
    norm_query_string text NOT NULL,
    application_name text NOT NULL,
    hints text NOT NULL,
    CONSTRAINT hints_pkey PRIMARY KEY((id) HASH)
)
SPLIT INTO 3 TABLETS;

-- For binary upgrade, handle extension membership the hard way
ALTER EXTENSION pg_hint_plan ADD TABLE hint_plan.hints;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE hint_plan.hints OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: hints_id_seq; Type: SEQUENCE; Schema: hint_plan; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16545'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16545'::pg_catalog.oid);

CREATE SEQUENCE hint_plan.hints_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

-- For binary upgrade, handle extension membership the hard way
ALTER EXTENSION pg_hint_plan ADD SEQUENCE hint_plan.hints_id_seq;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE hint_plan.hints_id_seq OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: hints_id_seq; Type: SEQUENCE OWNED BY; Schema: hint_plan; Owner: yugabyte_test
--

ALTER SEQUENCE hint_plan.hints_id_seq OWNED BY hint_plan.hints.id;


--
-- Name: chat_user; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16469'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16468'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16467'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16467'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16470'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16470'::pg_catalog.oid);

CREATE TABLE public.chat_user (
    "chatID" text NOT NULL,
    CONSTRAINT chat_user_pkey PRIMARY KEY(("chatID") HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.chat_user OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: hash_tbl_pk_with_include_clause; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16577'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16576'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16575'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16575'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16578'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16578'::pg_catalog.oid);

CREATE TABLE public.hash_tbl_pk_with_include_clause (
    k2 text NOT NULL,
    v double precision,
    k1 integer NOT NULL,
    CONSTRAINT hash_tbl_pk_with_include_clause_pkey PRIMARY KEY((k1, k2) HASH) INCLUDE (v)
)
SPLIT INTO 8 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.hash_tbl_pk_with_include_clause OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: hash_tbl_pk_with_multiple_included_columns; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16588'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16587'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16586'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16586'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16589'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16589'::pg_catalog.oid);

CREATE TABLE public.hash_tbl_pk_with_multiple_included_columns (
    col1 integer NOT NULL,
    col2 integer NOT NULL,
    col3 integer,
    col4 integer,
    CONSTRAINT hash_tbl_pk_with_multiple_included_columns_pkey PRIMARY KEY((col1) HASH, col2 ASC) INCLUDE (col3, col4)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.hash_tbl_pk_with_multiple_included_columns OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: level0; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16623'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16622'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16621'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16621'::pg_catalog.oid);

CREATE TABLE public.level0 (
    c1 integer,
    c2 text NOT NULL,
    c3 text,
    c4 text,
    CONSTRAINT level0_c1_cons CHECK ((c1 > 0)),
    CONSTRAINT level0_c1_cons2 CHECK ((c1 IS NULL)) NO INHERIT
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.level0 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: level1_0; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16626'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16625'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16624'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16624'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16627'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16627'::pg_catalog.oid);

CREATE TABLE public.level1_0 (
    c1 integer NOT NULL,
    c2 text NOT NULL,
    c3 text,
    c4 text,
    CONSTRAINT level1_0_pkey PRIMARY KEY(c1 ASC)
);

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c2'
  AND attrelid = 'public.level1_0'::pg_catalog.regclass;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c3'
  AND attrelid = 'public.level1_0'::pg_catalog.regclass;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c4'
  AND attrelid = 'public.level1_0'::pg_catalog.regclass;

-- For binary upgrade, set up inherited constraint.
ALTER TABLE ONLY public.level1_0 ADD CONSTRAINT level0_c1_cons CHECK ((c1 > 0));
UPDATE pg_catalog.pg_constraint
SET conislocal = false
WHERE contype = 'c' AND conname = 'level0_c1_cons'
  AND conrelid = 'public.level1_0'::pg_catalog.regclass;

-- For binary upgrade, set up inheritance this way.
ALTER TABLE ONLY public.level1_0 INHERIT public.level0;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.level1_0 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: level1_1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16631'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16630'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16629'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16629'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16632'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16632'::pg_catalog.oid);

CREATE TABLE public.level1_1 (
    c1 integer,
    c2 text NOT NULL,
    c3 text,
    c4 text,
    CONSTRAINT level1_1_c1_cons CHECK ((c1 >= 2)),
    CONSTRAINT level1_1_pkey PRIMARY KEY((c2) HASH)
)
SPLIT INTO 3 TABLETS;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c1'
  AND attrelid = 'public.level1_1'::pg_catalog.regclass;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c3'
  AND attrelid = 'public.level1_1'::pg_catalog.regclass;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c4'
  AND attrelid = 'public.level1_1'::pg_catalog.regclass;

-- For binary upgrade, set up inherited constraint.
ALTER TABLE ONLY public.level1_1 ADD CONSTRAINT level0_c1_cons CHECK ((c1 > 0));
UPDATE pg_catalog.pg_constraint
SET conislocal = false
WHERE contype = 'c' AND conname = 'level0_c1_cons'
  AND conrelid = 'public.level1_1'::pg_catalog.regclass;

-- For binary upgrade, set up inheritance this way.
ALTER TABLE ONLY public.level1_1 INHERIT public.level0;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.level1_1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: level2_0; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16637'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16636'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16635'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16635'::pg_catalog.oid);

CREATE TABLE public.level2_0 (
    c1 integer NOT NULL,
    c2 text NOT NULL,
    c3 text NOT NULL,
    c4 text
)
SPLIT INTO 3 TABLETS;

-- For binary upgrade, recreate inherited column.
UPDATE pg_catalog.pg_attribute
SET attislocal = false
WHERE attname = 'c4'
  AND attrelid = 'public.level2_0'::pg_catalog.regclass;

-- For binary upgrade, set up inherited constraint.
ALTER TABLE ONLY public.level2_0 ADD CONSTRAINT level0_c1_cons CHECK ((c1 > 0));
UPDATE pg_catalog.pg_constraint
SET conislocal = false
WHERE contype = 'c' AND conname = 'level0_c1_cons'
  AND conrelid = 'public.level2_0'::pg_catalog.regclass;

-- For binary upgrade, set up inheritance this way.
ALTER TABLE ONLY public.level2_0 INHERIT public.level1_0;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.level2_0 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: level2_1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16640'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16639'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16638'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16638'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16641'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16641'::pg_catalog.oid);

CREATE TABLE public.level2_1 (
    c1 integer NOT NULL,
    c2 text NOT NULL,
    c3 text NOT NULL,
    c4 text NOT NULL,
    CONSTRAINT level2_1_pkey PRIMARY KEY((c4) HASH)
)
SPLIT INTO 3 TABLETS;

-- For binary upgrade, set up inherited constraint.
ALTER TABLE ONLY public.level2_1 ADD CONSTRAINT level0_c1_cons CHECK ((c1 > 0));
UPDATE pg_catalog.pg_constraint
SET conislocal = false
WHERE contype = 'c' AND conname = 'level0_c1_cons'
  AND conrelid = 'public.level2_1'::pg_catalog.regclass;

-- For binary upgrade, set up inherited constraint.
ALTER TABLE ONLY public.level2_1 ADD CONSTRAINT level1_1_c1_cons CHECK ((c1 >= 2));
UPDATE pg_catalog.pg_constraint
SET conislocal = false
WHERE contype = 'c' AND conname = 'level1_1_c1_cons'
  AND conrelid = 'public.level2_1'::pg_catalog.regclass;

-- For binary upgrade, set up inheritance this way.
ALTER TABLE ONLY public.level2_1 INHERIT public.level1_0;
ALTER TABLE ONLY public.level2_1 INHERIT public.level1_1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.level2_1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: p1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16557'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16556'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16555'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16555'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16558'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16558'::pg_catalog.oid);

CREATE TABLE public.p1 (
    k integer NOT NULL,
    v text,
    CONSTRAINT p1_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.p1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: p2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16564'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16563'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16562'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16562'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16565'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16565'::pg_catalog.oid);

CREATE TABLE public.p2 (
    k integer NOT NULL,
    v text,
    CONSTRAINT p2_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.p2 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: part_uniq_const; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16593'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16592'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16591'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16591'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16594'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16594'::pg_catalog.oid);

CREATE TABLE public.part_uniq_const (
    v1 integer NOT NULL,
    v2 integer,
    v3 integer NOT NULL,
    CONSTRAINT part_uniq_const_pkey PRIMARY KEY((v1) HASH, v3 ASC)
)
PARTITION BY RANGE (v1)
SPLIT INTO 1 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.part_uniq_const OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: part_uniq_const_30_50; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16603'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16602'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16601'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16601'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16604'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16604'::pg_catalog.oid);

CREATE TABLE public.part_uniq_const_30_50 (
    v1 integer NOT NULL,
    v2 integer,
    v3 integer NOT NULL,
    CONSTRAINT part_uniq_const_30_50_pkey PRIMARY KEY((v1) HASH, v3 ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.part_uniq_const_30_50 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: part_uniq_const_50_100; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16598'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16597'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16596'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16596'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16599'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16599'::pg_catalog.oid);

CREATE TABLE public.part_uniq_const_50_100 (
    v1 integer NOT NULL,
    v2 integer,
    v3 integer NOT NULL,
    CONSTRAINT part_uniq_const_50_100_pkey PRIMARY KEY((v1) HASH, v3 ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.part_uniq_const_50_100 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: part_uniq_const_default; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16608'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16607'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16606'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16606'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16609'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16609'::pg_catalog.oid);

CREATE TABLE public.part_uniq_const_default (
    v1 integer NOT NULL,
    v2 integer,
    v3 integer NOT NULL,
    CONSTRAINT part_uniq_const_default_pkey PRIMARY KEY((v1) HASH, v3 ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.part_uniq_const_default OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: pre_split_range; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16536'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16535'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16534'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16534'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16537'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16537'::pg_catalog.oid);

CREATE TABLE public.pre_split_range (
    id integer NOT NULL,
    customer_id integer NOT NULL,
    company_name character varying(40) NOT NULL,
    contact_name character varying(30),
    contact_title character varying(30),
    address character varying(60),
    city character varying(15),
    region character varying(15),
    postal_code character varying(10),
    country character varying(15),
    phone character varying(24),
    fax character varying(24),
    more_col1 text,
    more_col2 text,
    more_col3 text,
    CONSTRAINT pre_split_range_pkey PRIMARY KEY(customer_id ASC)
)
SPLIT AT VALUES ((1000), (5000), (10000), (15000), (20000), (25000), (30000), (35000), (55000), (85000), (110000), (150000), (250000), (300000), (350000), (400000), (450000), (500000), (1000000));


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.pre_split_range OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: range_tbl_pk_with_include_clause; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16571'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16570'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16569'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16569'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16572'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16572'::pg_catalog.oid);

CREATE TABLE public.range_tbl_pk_with_include_clause (
    k2 text NOT NULL,
    v double precision,
    k1 integer NOT NULL,
    CONSTRAINT range_tbl_pk_with_include_clause_pkey PRIMARY KEY(k1 ASC, k2 ASC) INCLUDE (v)
)
SPLIT AT VALUES ((1, '1'), (100, '100'));


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.range_tbl_pk_with_include_clause OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: range_tbl_pk_with_multiple_included_columns; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16583'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16582'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16581'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16581'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16584'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16584'::pg_catalog.oid);

CREATE TABLE public.range_tbl_pk_with_multiple_included_columns (
    col1 integer NOT NULL,
    col2 integer NOT NULL,
    col3 integer,
    col4 integer,
    CONSTRAINT range_tbl_pk_with_multiple_included_columns_pkey PRIMARY KEY(col1 ASC, col2 ASC) INCLUDE (col3, col4)
);


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.range_tbl_pk_with_multiple_included_columns OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: range_test; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16764'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16763'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16762'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16762'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16766'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16766'::pg_catalog.oid);

CREATE TABLE public.range_test (
    id integer NOT NULL,
    num_range int4range,
    CONSTRAINT range_test_pkey PRIMARY KEY((id) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.range_test OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: range_test_id_seq; Type: SEQUENCE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16761'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16761'::pg_catalog.oid);

CREATE SEQUENCE public.range_test_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.range_test_id_seq OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: range_test_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: yugabyte_test
--

ALTER SEQUENCE public.range_test_id_seq OWNED BY public.range_test.id;


--
-- Name: rls_private; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16461'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16460'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16459'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16459'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16462'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16462'::pg_catalog.oid);

CREATE TABLE public.rls_private (
    k integer NOT NULL,
    v text,
    CONSTRAINT rls_private_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;

ALTER TABLE ONLY public.rls_private FORCE ROW LEVEL SECURITY;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.rls_private OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: rls_public; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16456'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16455'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16454'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16454'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16457'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16457'::pg_catalog.oid);

CREATE TABLE public.rls_public (
    k integer NOT NULL,
    v text,
    CONSTRAINT rls_public_pkey PRIMARY KEY((k) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.rls_public OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16387'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16386'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16385'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16385'::pg_catalog.oid);

CREATE TABLE public.tbl1 (
    a integer NOT NULL,
    b integer
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl10; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16436'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16435'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16434'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16434'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16437'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16437'::pg_catalog.oid);

CREATE TABLE public.tbl10 (
    a integer NOT NULL,
    b integer NOT NULL,
    c integer NOT NULL,
    d integer,
    CONSTRAINT tbl10_pkey PRIMARY KEY((a, c) HASH, b ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl10 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl11; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16441'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16440'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16439'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16439'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16442'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16442'::pg_catalog.oid);

CREATE TABLE public.tbl11 (
    a integer NOT NULL,
    b integer NOT NULL,
    c integer,
    CONSTRAINT tbl11_pkey PRIMARY KEY(a DESC, b ASC)
);


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl11 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl12; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16446'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16445'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16444'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16444'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16447'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16447'::pg_catalog.oid);

CREATE TABLE public.tbl12 (
    a integer NOT NULL,
    b integer,
    c integer NOT NULL,
    d integer NOT NULL,
    CONSTRAINT tbl12_pkey PRIMARY KEY(a ASC, d DESC, c DESC)
);


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl12 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl13; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16451'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16450'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16449'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16449'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16452'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16452'::pg_catalog.oid);

CREATE TABLE public.tbl13 (
    a integer,
    b integer NOT NULL,
    c integer NOT NULL,
    d integer,
    CONSTRAINT tbl13_pkey PRIMARY KEY((b, c) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl13 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl1_a_seq; Type: SEQUENCE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16384'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16384'::pg_catalog.oid);

CREATE SEQUENCE public.tbl1_a_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl1_a_seq OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl1_a_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: yugabyte_test
--

ALTER SEQUENCE public.tbl1_a_seq OWNED BY public.tbl1.a;


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

CREATE TABLE public.tbl2 (
    a integer NOT NULL
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl2 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl2_a_seq; Type: SEQUENCE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16389'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16389'::pg_catalog.oid);

CREATE SEQUENCE public.tbl2_a_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl2_a_seq OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl2_a_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: yugabyte_test
--

ALTER SEQUENCE public.tbl2_a_seq OWNED BY public.tbl2.a;


--
-- Name: tbl3; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16396'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16395'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16394'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16394'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16397'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16397'::pg_catalog.oid);

CREATE TABLE public.tbl3 (
    a integer NOT NULL,
    b integer,
    CONSTRAINT tbl3_pkey PRIMARY KEY(a ASC)
);


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl3 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl4; Type: TABLE; Schema: public; Owner: yugabyte_test
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

CREATE TABLE public.tbl4 (
    a integer NOT NULL,
    b integer NOT NULL,
    CONSTRAINT tbl4_pkey PRIMARY KEY((a) HASH, b ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl4 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl5; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16406'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16405'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16404'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16404'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16407'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16407'::pg_catalog.oid);

CREATE TABLE public.tbl5 (
    a integer NOT NULL,
    b integer,
    c integer,
    CONSTRAINT tbl5_pkey PRIMARY KEY((a) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl5 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl6; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16411'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16410'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16409'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16409'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16412'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16412'::pg_catalog.oid);

CREATE TABLE public.tbl6 (
    a integer NOT NULL,
    CONSTRAINT tbl6_pkey PRIMARY KEY((a) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl6 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl7; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16416'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16415'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16414'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16414'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16417'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16417'::pg_catalog.oid);

CREATE TABLE public.tbl7 (
    a integer,
    b integer NOT NULL,
    c integer NOT NULL,
    d integer,
    CONSTRAINT tbl7_pkey PRIMARY KEY((b) HASH, c ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl7 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl8; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16421'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16420'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16419'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16419'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16422'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16422'::pg_catalog.oid);

CREATE TABLE public.tbl8 (
    a integer NOT NULL,
    b integer,
    c integer,
    d integer NOT NULL,
    CONSTRAINT tbl8_pkey PRIMARY KEY((a) HASH, d ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl8 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tbl9; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16431'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16430'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16429'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16429'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16432'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16432'::pg_catalog.oid);

CREATE TABLE public.tbl9 (
    a integer NOT NULL,
    b integer NOT NULL,
    c integer,
    CONSTRAINT tbl9_pkey PRIMARY KEY((a, b) HASH)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tbl9 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tgroup_after_options; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16501'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16500'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16499'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16499'::pg_catalog.oid);

CREATE TABLE public.tgroup_after_options (
    a integer
)
WITH (parallel_workers='2', colocation_id='20002')
TABLEGROUP grp1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_after_options OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_empty_options; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16507'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16506'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16505'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16505'::pg_catalog.oid);

CREATE TABLE public.tgroup_empty_options (
    a integer
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_empty_options OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_in_between_options; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16504'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16503'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16502'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16502'::pg_catalog.oid);

CREATE TABLE public.tgroup_in_between_options (
    a integer
)
WITH (parallel_workers='2', autovacuum_enabled='true', colocation_id='20003')
TABLEGROUP grp1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_in_between_options OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_no_options_and_tgroup; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16483'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16482'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16481'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16481'::pg_catalog.oid);

CREATE TABLE public.tgroup_no_options_and_tgroup (
    a integer
)
WITH (colocation_id='20001')
TABLEGROUP grp1;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_no_options_and_tgroup OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_one_option; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16486'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16485'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16484'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16484'::pg_catalog.oid);

CREATE TABLE public.tgroup_one_option (
    a integer
)
WITH (autovacuum_enabled='true')
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_one_option OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_one_option_and_tgroup; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16489'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16488'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16487'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16487'::pg_catalog.oid);

CREATE TABLE public.tgroup_one_option_and_tgroup (
    a integer
)
WITH (autovacuum_enabled='true', colocation_id='20001')
TABLEGROUP grp2;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_one_option_and_tgroup OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_options; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16492'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16491'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16490'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16490'::pg_catalog.oid);

CREATE TABLE public.tgroup_options (
    a integer
)
WITH (autovacuum_enabled='true', parallel_workers='2')
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_options OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_options_and_tgroup; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16495'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16494'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16493'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16493'::pg_catalog.oid);

CREATE TABLE public.tgroup_options_and_tgroup (
    a integer
)
WITH (autovacuum_enabled='true', parallel_workers='2', colocation_id='20002')
TABLEGROUP grp2;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_options_and_tgroup OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_options_tgroup_and_custom_colocation_id; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16498'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16497'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16496'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16496'::pg_catalog.oid);

CREATE TABLE public.tgroup_options_tgroup_and_custom_colocation_id (
    a integer
)
WITH (autovacuum_enabled='true', parallel_workers='2', colocation_id='100500')
TABLEGROUP grp2;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_options_tgroup_and_custom_colocation_id OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: tgroup_with_spc; Type: TABLE; Schema: public; Owner: tablegroup_test_user
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16510'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16509'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16508'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16508'::pg_catalog.oid);

CREATE TABLE public.tgroup_with_spc (
    a integer
)
WITH (colocation_id='20001')
TABLEGROUP grp_with_spc;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tgroup_with_spc OWNER TO tablegroup_test_user;
\else
    \echo 'Skipping owner privilege due to missing role:' tablegroup_test_user
\endif
\endif

--
-- Name: th1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16513'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16512'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16511'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16511'::pg_catalog.oid);

CREATE TABLE public.th1 (
    a integer,
    b text,
    c double precision
)
SPLIT INTO 2 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.th1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: th2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16516'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16515'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16514'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16514'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16517'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16517'::pg_catalog.oid);

CREATE TABLE public.th2 (
    a integer NOT NULL,
    b text NOT NULL,
    c double precision,
    CONSTRAINT th2_pkey PRIMARY KEY((a) HASH, b ASC)
)
SPLIT INTO 3 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.th2 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: th3; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16521'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16520'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16519'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16519'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16522'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16522'::pg_catalog.oid);

CREATE TABLE public.th3 (
    a integer NOT NULL,
    b text NOT NULL,
    c double precision,
    CONSTRAINT th3_pkey PRIMARY KEY((a, b) HASH)
)
SPLIT INTO 4 TABLETS;


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.th3 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tr1; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16526'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16525'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16524'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16524'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16527'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16527'::pg_catalog.oid);

CREATE TABLE public.tr1 (
    a integer NOT NULL,
    b text,
    c double precision,
    CONSTRAINT tr1_pkey PRIMARY KEY(a ASC)
)
SPLIT AT VALUES ((1), (100));


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tr1 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: tr2; Type: TABLE; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16531'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16530'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16529'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16529'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16532'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16532'::pg_catalog.oid);

CREATE TABLE public.tr2 (
    a integer NOT NULL,
    b text NOT NULL,
    c double precision NOT NULL,
    CONSTRAINT tr2_pkey PRIMARY KEY(a DESC, b ASC, c DESC)
)
SPLIT AT VALUES ((100, 'a', 2.5), (50, 'n', MINVALUE), (1, 'z', -5.12));


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.tr2 OWNER TO yugabyte_test;
\else
    \echo 'Skipping owner privilege due to missing role:' yugabyte_test
\endif
\endif

--
-- Name: uaccount; Type: TABLE; Schema: public; Owner: regress_rls_alice
--


-- For binary upgrade, must preserve pg_type oid
SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('16474'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_type array oid
SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('16473'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('16472'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('16472'::pg_catalog.oid);


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16475'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16475'::pg_catalog.oid);

CREATE TABLE public.uaccount (
    pguser name NOT NULL,
    seclv integer,
    CONSTRAINT uaccount_pkey PRIMARY KEY(pguser ASC)
);


\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    ALTER TABLE public.uaccount OWNER TO regress_rls_alice;
\else
    \echo 'Skipping owner privilege due to missing role:' regress_rls_alice
\endif
\endif

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
-- Name: hints id; Type: DEFAULT; Schema: hint_plan; Owner: yugabyte_test
--

ALTER TABLE ONLY hint_plan.hints ALTER COLUMN id SET DEFAULT nextval('hint_plan.hints_id_seq'::regclass);


--
-- Name: range_test id; Type: DEFAULT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.range_test ALTER COLUMN id SET DEFAULT nextval('public.range_test_id_seq'::regclass);


--
-- Name: tbl1 a; Type: DEFAULT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.tbl1 ALTER COLUMN a SET DEFAULT nextval('public.tbl1_a_seq'::regclass);


--
-- Name: tbl2 a; Type: DEFAULT; Schema: public; Owner: yugabyte_test
--

ALTER TABLE ONLY public.tbl2 ALTER COLUMN a SET DEFAULT nextval('public.tbl2_a_seq'::regclass);


--
-- Data for Name: hints; Type: TABLE DATA; Schema: hint_plan; Owner: yugabyte_test
--

COPY hint_plan.hints (id, norm_query_string, application_name, hints) FROM stdin;
\.


--
-- Data for Name: chat_user; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.chat_user ("chatID") FROM stdin;
\.


--
-- Data for Name: hash_tbl_pk_with_include_clause; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.hash_tbl_pk_with_include_clause (k2, v, k1) FROM stdin;
\.


--
-- Data for Name: hash_tbl_pk_with_multiple_included_columns; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.hash_tbl_pk_with_multiple_included_columns (col1, col2, col3, col4) FROM stdin;
\.


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

COPY public.p1 (k, v) FROM stdin;
\.


--
-- Data for Name: p2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.p2 (k, v) FROM stdin;
\.


--
-- Data for Name: part_uniq_const_30_50; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_30_50 (v1, v2, v3) FROM stdin;
31	231	231
\.


--
-- Data for Name: part_uniq_const_50_100; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_50_100 (v1, v2, v3) FROM stdin;
51	151	151
\.


--
-- Data for Name: part_uniq_const_default; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.part_uniq_const_default (v1, v2, v3) FROM stdin;
1	1001	1001
\.


--
-- Data for Name: pre_split_range; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.pre_split_range (id, customer_id, company_name, contact_name, contact_title, address, city, region, postal_code, country, phone, fax, more_col1, more_col2, more_col3) FROM stdin;
\.


--
-- Data for Name: range_tbl_pk_with_include_clause; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.range_tbl_pk_with_include_clause (k2, v, k1) FROM stdin;
\.


--
-- Data for Name: range_tbl_pk_with_multiple_included_columns; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.range_tbl_pk_with_multiple_included_columns (col1, col2, col3, col4) FROM stdin;
\.


--
-- Data for Name: range_test; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.range_test (id, num_range) FROM stdin;
5	empty
1	[1,11)
6	[30,101)
7	[50,61)
9	[80,86)
10	[90,101)
4	[25,26)
2	[2,6)
8	[70,76)
3	[15,21)
\.


--
-- Data for Name: rls_private; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.rls_private (k, v) FROM stdin;
\.


--
-- Data for Name: rls_public; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.rls_public (k, v) FROM stdin;
\.


--
-- Data for Name: tbl1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl1 (a, b) FROM stdin;
1	100
\.


--
-- Data for Name: tbl10; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl10 (a, b, c, d) FROM stdin;
\.


--
-- Data for Name: tbl11; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl11 (a, b, c) FROM stdin;
\.


--
-- Data for Name: tbl12; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl12 (a, b, c, d) FROM stdin;
\.


--
-- Data for Name: tbl13; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl13 (a, b, c, d) FROM stdin;
\.


--
-- Data for Name: tbl2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl2 (a) FROM stdin;
\.


--
-- Data for Name: tbl3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl3 (a, b) FROM stdin;
\.


--
-- Data for Name: tbl4; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl4 (a, b) FROM stdin;
\.


--
-- Data for Name: tbl5; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl5 (a, b, c) FROM stdin;
4	7	16
\.


--
-- Data for Name: tbl6; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl6 (a) FROM stdin;
\.


--
-- Data for Name: tbl7; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl7 (a, b, c, d) FROM stdin;
\.


--
-- Data for Name: tbl8; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl8 (a, b, c, d) FROM stdin;
\.


--
-- Data for Name: tbl9; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tbl9 (a, b, c) FROM stdin;
\.


--
-- Data for Name: tgroup_after_options; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_after_options (a) FROM stdin;
\.


--
-- Data for Name: tgroup_empty_options; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_empty_options (a) FROM stdin;
\.


--
-- Data for Name: tgroup_in_between_options; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_in_between_options (a) FROM stdin;
\.


--
-- Data for Name: tgroup_no_options_and_tgroup; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_no_options_and_tgroup (a) FROM stdin;
\.


--
-- Data for Name: tgroup_one_option; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_one_option (a) FROM stdin;
\.


--
-- Data for Name: tgroup_one_option_and_tgroup; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_one_option_and_tgroup (a) FROM stdin;
\.


--
-- Data for Name: tgroup_options; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_options (a) FROM stdin;
\.


--
-- Data for Name: tgroup_options_and_tgroup; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_options_and_tgroup (a) FROM stdin;
\.


--
-- Data for Name: tgroup_options_tgroup_and_custom_colocation_id; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_options_tgroup_and_custom_colocation_id (a) FROM stdin;
\.


--
-- Data for Name: tgroup_with_spc; Type: TABLE DATA; Schema: public; Owner: tablegroup_test_user
--

COPY public.tgroup_with_spc (a) FROM stdin;
\.


--
-- Data for Name: th1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.th1 (a, b, c) FROM stdin;
\.


--
-- Data for Name: th2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.th2 (a, b, c) FROM stdin;
\.


--
-- Data for Name: th3; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.th3 (a, b, c) FROM stdin;
\.


--
-- Data for Name: tr1; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tr1 (a, b, c) FROM stdin;
\.


--
-- Data for Name: tr2; Type: TABLE DATA; Schema: public; Owner: yugabyte_test
--

COPY public.tr2 (a, b, c) FROM stdin;
\.


--
-- Data for Name: uaccount; Type: TABLE DATA; Schema: public; Owner: regress_rls_alice
--

COPY public.uaccount (pguser, seclv) FROM stdin;
\.


--
-- Name: hints_id_seq; Type: SEQUENCE SET; Schema: hint_plan; Owner: yugabyte_test
--

SELECT pg_catalog.setval('hint_plan.hints_id_seq', 1, false);


--
-- Name: range_test_id_seq; Type: SEQUENCE SET; Schema: public; Owner: yugabyte_test
--

SELECT pg_catalog.setval('public.range_test_id_seq', 10, true);


--
-- Name: tbl1_a_seq; Type: SEQUENCE SET; Schema: public; Owner: yugabyte_test
--

SELECT pg_catalog.setval('public.tbl1_a_seq', 1, true);


--
-- Name: tbl2_a_seq; Type: SEQUENCE SET; Schema: public; Owner: yugabyte_test
--

SELECT pg_catalog.setval('public.tbl2_a_seq', 1, false);


--
-- Statistics for Name: hints; Type: STATISTICS DATA; Schema: hint_plan; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'hint_plan',
	'relname', 'hints',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: chat_user; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'chat_user',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: hash_tbl_pk_with_include_clause; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'hash_tbl_pk_with_include_clause',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: hash_tbl_pk_with_multiple_included_columns; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'hash_tbl_pk_with_multiple_included_columns',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


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
-- Statistics for Name: pre_split_range; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'pre_split_range',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_tbl_pk_with_include_clause; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_tbl_pk_with_include_clause',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_tbl_pk_with_multiple_included_columns; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_tbl_pk_with_multiple_included_columns',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_test; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_test',
	'relpages', '0'::integer,
	'reltuples', '10'::real,
	'relallvisible', '0'::integer
);
SELECT * FROM pg_catalog.pg_restore_attribute_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_test',
	'attname', 'id',
	'inherited', 'f'::boolean,
	'null_frac', '0'::real,
	'avg_width', '4'::integer,
	'n_distinct', '-1'::real,
	'histogram_bounds', '{1,2,3,4,5,6,7,8,9,10}'::text,
	'correlation', '0.018181818'::real
);
SELECT * FROM pg_catalog.pg_restore_attribute_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_test',
	'attname', 'num_range',
	'inherited', 'f'::boolean,
	'null_frac', '0'::real,
	'avg_width', '13'::integer,
	'n_distinct', '-1'::real,
	'range_length_histogram', '{1,4,6,6,6,10,11,11,71}'::text,
	'range_empty_frac', '0.1'::real,
	'range_bounds_histogram', '{"[1,6)","[2,11)","[15,21)","[25,26)","[30,61)","[50,76)","[70,86)","[80,101)","[90,101)"}'::text
);


--
-- Statistics for Name: rls_private; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'rls_private',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: rls_public; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'rls_public',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl10; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl10',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl11; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl11',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl12; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl12',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl13; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl13',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl2',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl3; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl3',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl4; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl4',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl5; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl5',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl6; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl6',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl7; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl7',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl9; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl9',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_after_options; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_after_options',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_empty_options; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_empty_options',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_in_between_options; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_in_between_options',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_no_options_and_tgroup; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_no_options_and_tgroup',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_one_option; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_one_option',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_one_option_and_tgroup; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_one_option_and_tgroup',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_options; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_options',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_options_and_tgroup; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_options_and_tgroup',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_options_tgroup_and_custom_colocation_id; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_options_tgroup_and_custom_colocation_id',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tgroup_with_spc; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tgroup_with_spc',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th2',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th3; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th3',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr1; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr1',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr2',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: uaccount; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'uaccount',
	'relpages', '0'::integer,
	'reltuples', '-1'::real,
	'relallvisible', '0'::integer
);


--
-- Name: hints_norm_and_app; Type: INDEX; Schema: hint_plan; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16552'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16552'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY hints_norm_and_app ON hint_plan.hints USING lsm (norm_query_string HASH, application_name ASC) SPLIT INTO 3 TABLETS;


--
-- Name: p1 c1; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16560'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16560'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY c1 ON public.p1 USING lsm (v ASC) SPLIT AT VALUES (('foo'), ('qux'));

ALTER TABLE ONLY public.p1
    ADD CONSTRAINT c1 UNIQUE USING INDEX c1;


--
-- Name: p2 c2; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16567'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16567'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY c2 ON public.p2 USING lsm (v HASH) SPLIT INTO 10 TABLETS;

ALTER TABLE ONLY public.p2
    ADD CONSTRAINT c2 UNIQUE USING INDEX c2;


--
-- Name: level1_1_c3_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16634'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16634'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY level1_1_c3_idx ON public.level1_1 USING lsm (c3 DESC);


--
-- Name: level2_1_c3_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16643'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16643'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY level2_1_c3_idx ON public.level2_1 USING lsm (c3 ASC);


--
-- Name: non_unique_idx_with_include_clause; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16580'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16580'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY non_unique_idx_with_include_clause ON public.hash_tbl_pk_with_include_clause USING lsm (k1 HASH, k2 ASC) INCLUDE (v) SPLIT INTO 3 TABLETS;


--
-- Name: part_uniq_const part_uniq_const_unique; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16611'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16611'::pg_catalog.oid);

ALTER TABLE ONLY public.part_uniq_const
    ADD CONSTRAINT part_uniq_const_unique UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_30_50 part_uniq_const_30_50_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16613'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16613'::pg_catalog.oid);

ALTER TABLE ONLY public.part_uniq_const_30_50
    ADD CONSTRAINT part_uniq_const_30_50_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_50_100 part_uniq_const_50_100_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16615'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16615'::pg_catalog.oid);

ALTER TABLE ONLY public.part_uniq_const_50_100
    ADD CONSTRAINT part_uniq_const_50_100_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: part_uniq_const_50_100 part_uniq_const_50_100_v2_uniq; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16619'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16619'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY part_uniq_const_50_100_v2_uniq ON public.part_uniq_const_50_100 USING lsm (v2 ASC);

ALTER TABLE ONLY public.part_uniq_const_50_100
    ADD CONSTRAINT part_uniq_const_50_100_v2_uniq UNIQUE USING INDEX part_uniq_const_50_100_v2_uniq;


--
-- Name: part_uniq_const_default part_uniq_const_default_v1_v2_key; Type: CONSTRAINT; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16617'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16617'::pg_catalog.oid);

ALTER TABLE ONLY public.part_uniq_const_default
    ADD CONSTRAINT part_uniq_const_default_v1_v2_key UNIQUE  (v1, v2);


--
-- Name: tbl8_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16424'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16424'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl8_idx ON public.tbl8 USING lsm ((b, c) HASH) SPLIT INTO 3 TABLETS;


--
-- Name: tbl8_idx2; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16425'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16425'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl8_idx2 ON public.tbl8 USING lsm (a HASH, b ASC) SPLIT INTO 3 TABLETS;


--
-- Name: tbl8_idx3; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16426'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16426'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl8_idx3 ON public.tbl8 USING lsm (b ASC);


--
-- Name: tbl8_idx4; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16427'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16427'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl8_idx4 ON public.tbl8 USING lsm (b DESC);


--
-- Name: tbl8_idx5; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16428'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16428'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tbl8_idx5 ON public.tbl8 USING lsm (c HASH) SPLIT INTO 3 TABLETS;


--
-- Name: th2_c_b_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16539'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16539'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY th2_c_b_idx ON public.th2 USING lsm (c HASH, b DESC) SPLIT INTO 4 TABLETS;


--
-- Name: th3_c_b_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16540'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16540'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY th3_c_b_idx ON public.th3 USING lsm ((c, b) HASH) SPLIT INTO 3 TABLETS;


--
-- Name: tr2_c_b_a_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16542'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16542'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tr2_c_b_a_idx ON public.tr2 USING lsm (c ASC, b DESC, a ASC) SPLIT AT VALUES ((-5.12, 'z', 1), (-0.75, 'l', MINVALUE), (2.5, 'a', 100));


--
-- Name: tr2_c_idx; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16541'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16541'::pg_catalog.oid);

CREATE INDEX NONCONCURRENTLY tr2_c_idx ON public.tr2 USING lsm (c DESC) SPLIT AT VALUES ((100.5), (1.5));


--
-- Name: unique_idx_with_include_clause; Type: INDEX; Schema: public; Owner: yugabyte_test
--


-- For binary upgrade, must preserve pg_class oids and relfilenodes
SELECT pg_catalog.binary_upgrade_set_next_index_pg_class_oid('16574'::pg_catalog.oid);
SELECT pg_catalog.binary_upgrade_set_next_index_relfilenode('16574'::pg_catalog.oid);

CREATE UNIQUE INDEX NONCONCURRENTLY unique_idx_with_include_clause ON public.range_tbl_pk_with_include_clause USING lsm (k1 HASH, k2 ASC) INCLUDE (v) SPLIT INTO 3 TABLETS;


--
-- Name: part_uniq_const_30_50_pkey; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_pkey ATTACH PARTITION public.part_uniq_const_30_50_pkey;


--
-- Name: part_uniq_const_30_50_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_30_50_v1_v2_key;


--
-- Name: part_uniq_const_50_100_pkey; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_pkey ATTACH PARTITION public.part_uniq_const_50_100_pkey;


--
-- Name: part_uniq_const_50_100_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_50_100_v1_v2_key;


--
-- Name: part_uniq_const_default_pkey; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_pkey ATTACH PARTITION public.part_uniq_const_default_pkey;


--
-- Name: part_uniq_const_default_v1_v2_key; Type: INDEX ATTACH; Schema: public; Owner: yugabyte_test
--

ALTER INDEX public.part_uniq_const_unique ATTACH PARTITION public.part_uniq_const_default_v1_v2_key;


--
-- Name: hints yb_invalidate_hint_plan_cache; Type: TRIGGER; Schema: hint_plan; Owner: yugabyte_test
--

CREATE TRIGGER yb_invalidate_hint_plan_cache AFTER INSERT OR DELETE OR UPDATE OR TRUNCATE ON hint_plan.hints FOR EACH STATEMENT EXECUTE FUNCTION hint_plan.yb_cache_invalidate();


--
-- Name: uaccount account_policies; Type: POLICY; Schema: public; Owner: regress_rls_alice
--

CREATE POLICY account_policies ON public.uaccount USING ((pguser = CURRENT_USER));


--
-- Name: rls_public p1; Type: POLICY; Schema: public; Owner: yugabyte_test
--

CREATE POLICY p1 ON public.rls_public USING (((k % 2) = 0));


--
-- Name: rls_private p2; Type: POLICY; Schema: public; Owner: yugabyte_test
--

CREATE POLICY p2 ON public.rls_private FOR INSERT WITH CHECK (((k % 2) = 1));


--
-- Name: rls_private p3; Type: POLICY; Schema: public; Owner: yugabyte_test
--

CREATE POLICY p3 ON public.rls_private FOR UPDATE USING (((k % 2) = 1));


--
-- Name: rls_public p4; Type: POLICY; Schema: public; Owner: yugabyte_test
--

\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    CREATE POLICY p4 ON public.rls_public FOR UPDATE TO rls_user USING ((v = CURRENT_USER));
\else
    \echo 'Skipping create policy due to missing role:' rls_user
\endif

\endif


--
-- Name: rls_private; Type: ROW SECURITY; Schema: public; Owner: yugabyte_test
--

ALTER TABLE public.rls_private ENABLE ROW LEVEL SECURITY;

--
-- Name: rls_public; Type: ROW SECURITY; Schema: public; Owner: yugabyte_test
--

ALTER TABLE public.rls_public ENABLE ROW LEVEL SECURITY;

--
-- Name: uaccount; Type: ROW SECURITY; Schema: public; Owner: regress_rls_alice
--

ALTER TABLE public.uaccount ENABLE ROW LEVEL SECURITY;

--
-- Name: pg_hint_plan; Type: EXTENSION; Schema: -; Owner: 
--

-- YB: ensure extconfig field for extension: pg_hint_plan in pg_extension catalog is correct
UPDATE pg_extension SET extconfig = ARRAY['hint_plan.hints'::regclass::oid,'hint_plan.hints_id_seq'::regclass::oid]::oid[] WHERE extname = 'pg_hint_plan';


--
-- Name: SCHEMA hint_plan; Type: ACL; Schema: -; Owner: yugabyte_test
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT USAGE ON SCHEMA hint_plan TO PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: SCHEMA public; Type: ACL; Schema: -; Owner: pg_database_owner
--

\if :use_roles
REVOKE USAGE ON SCHEMA public FROM PUBLIC;
GRANT ALL ON SCHEMA public TO PUBLIC;
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    GRANT CREATE ON SCHEMA public TO regress_rls_alice;
\else
    \echo 'Skipping grant privilege due to missing role:' regress_rls_alice
\endif

\endif


--
-- Name: FUNCTION pg_stat_statements_reset(userid oid, dbid oid, queryid bigint); Type: ACL; Schema: pg_catalog; Owner: postgres
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
REVOKE ALL ON FUNCTION pg_catalog.pg_stat_statements_reset(userid oid, dbid oid, queryid bigint) FROM PUBLIC;
SELECT pg_catalog.binary_upgrade_set_record_init_privs(false);
\endif


--
-- Name: TABLE hints; Type: ACL; Schema: hint_plan; Owner: yugabyte_test
--

\if :use_roles
SELECT pg_catalog.binary_upgrade_set_record_init_privs(true);
GRANT SELECT ON TABLE hint_plan.hints TO PUBLIC;
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
-- Name: TABLE range_test; Type: ACL; Schema: public; Owner: yugabyte_test
--

\if :use_roles
GRANT SELECT ON TABLE public.range_test TO PUBLIC;
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    GRANT UPDATE ON TABLE public.range_test TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user
\endif

\endif


--
-- Name: TABLE rls_private; Type: ACL; Schema: public; Owner: yugabyte_test
--

\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    GRANT SELECT ON TABLE public.rls_private TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user
\endif

\endif


--
-- Name: TABLE rls_public; Type: ACL; Schema: public; Owner: yugabyte_test
--

\if :use_roles
GRANT ALL ON TABLE public.rls_public TO PUBLIC;
\endif


--
-- Name: TABLE tbl13; Type: ACL; Schema: public; Owner: yugabyte_test
--

\if :use_roles
SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    GRANT ALL ON TABLE public.tbl13 TO regress_rls_alice WITH GRANT OPTION;
\else
    \echo 'Skipping grant privilege due to missing role:' regress_rls_alice
\endif

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'tablegroup_test_user') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    SET SESSION AUTHORIZATION regress_rls_alice;
    GRANT ALL ON TABLE public.tbl13 TO tablegroup_test_user;
    RESET SESSION AUTHORIZATION;
\else
    \echo 'Skipping grant privilege due to missing role:' tablegroup_test_user 'OR' regress_rls_alice
\endif

\endif


--
-- Name: DEFAULT PRIVILEGES FOR FUNCTIONS; Type: DEFAULT ACL; Schema: public; Owner: regress_rls_alice
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE regress_rls_alice IN SCHEMA public GRANT ALL ON FUNCTIONS  TO PUBLIC;
\else
    \echo 'Skipping grant privilege due to missing role:' regress_rls_alice
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: public; Owner: yugabyte_test
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE yugabyte_test IN SCHEMA public GRANT SELECT ON TABLES  TO PUBLIC;
\else
    \echo 'Skipping grant privilege due to missing role:' yugabyte_test
\endif

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE yugabyte_test IN SCHEMA public GRANT UPDATE ON TABLES  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user 'OR' yugabyte_test
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: public; Owner: regress_rls_alice
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE regress_rls_alice IN SCHEMA public GRANT DELETE ON TABLES  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user 'OR' regress_rls_alice
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: public; Owner: rls_user
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE rls_user IN SCHEMA public GRANT SELECT ON TABLES  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TYPES; Type: DEFAULT ACL; Schema: -; Owner: regress_rls_alice
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE regress_rls_alice REVOKE ALL ON TYPES  FROM PUBLIC;
\else
    \echo 'Skipping revoke privilege due to missing role:' regress_rls_alice
\endif



--
-- Name: DEFAULT PRIVILEGES FOR SCHEMAS; Type: DEFAULT ACL; Schema: -; Owner: yugabyte_test
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'yugabyte_test') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE yugabyte_test GRANT USAGE ON SCHEMAS  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user 'OR' yugabyte_test
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: -; Owner: regress_rls_alice
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AND EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'regress_rls_alice') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE regress_rls_alice GRANT SELECT ON TABLES  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user 'OR' regress_rls_alice
\endif



--
-- Name: DEFAULT PRIVILEGES FOR TABLES; Type: DEFAULT ACL; Schema: -; Owner: rls_user
--

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE rls_user REVOKE ALL ON TABLES  FROM rls_user;
\else
    \echo 'Skipping revoke privilege due to missing role:' rls_user
\endif

SELECT EXISTS(SELECT 1 FROM pg_roles WHERE rolname = 'rls_user') AS role_exists \gset
\if :role_exists
    ALTER DEFAULT PRIVILEGES FOR ROLE rls_user GRANT SELECT,REFERENCES,TRIGGER,TRUNCATE,UPDATE ON TABLES  TO rls_user;
\else
    \echo 'Skipping grant privilege due to missing role:' rls_user
\endif



--
-- Statistics for Name: hints_norm_and_app; Type: STATISTICS DATA; Schema: hint_plan; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'hint_plan',
	'relname', 'hints_norm_and_app',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: hints_pkey; Type: STATISTICS DATA; Schema: hint_plan; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'hint_plan',
	'relname', 'hints_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


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
-- Statistics for Name: chat_user_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'chat_user_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: hash_tbl_pk_with_include_clause_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'hash_tbl_pk_with_include_clause_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: hash_tbl_pk_with_multiple_included_columns_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'hash_tbl_pk_with_multiple_included_columns_pkey',
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
-- Statistics for Name: non_unique_idx_with_include_clause; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'non_unique_idx_with_include_clause',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: p1_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'p1_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: p2_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'p2_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: part_uniq_const_30_50_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_30_50_pkey',
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
-- Statistics for Name: part_uniq_const_50_100_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_50_100_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
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
-- Statistics for Name: part_uniq_const_default_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_default_pkey',
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
-- Statistics for Name: part_uniq_const_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'part_uniq_const_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
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
-- Statistics for Name: pre_split_range_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'pre_split_range_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_tbl_pk_with_include_clause_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_tbl_pk_with_include_clause_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_tbl_pk_with_multiple_included_columns_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_tbl_pk_with_multiple_included_columns_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: range_test_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'range_test_pkey',
	'relpages', '0'::integer,
	'reltuples', '10'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: rls_private_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'rls_private_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: rls_public_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'rls_public_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl10_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl10_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl11_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl11_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl12_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl12_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl13_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl13_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl3_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl3_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl4_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl4_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl5_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl5_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl6_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl6_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl7_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl7_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_idx2; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_idx2',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_idx3; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_idx3',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_idx4; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_idx4',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_idx5; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_idx5',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl8_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl8_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tbl9_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tbl9_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th2_c_b_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th2_c_b_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th2_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th2_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th3_c_b_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th3_c_b_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: th3_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'th3_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr1_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr1_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr2_c_b_a_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr2_c_b_a_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr2_c_idx; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr2_c_idx',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: tr2_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'tr2_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: uaccount_pkey; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'uaccount_pkey',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


--
-- Statistics for Name: unique_idx_with_include_clause; Type: STATISTICS DATA; Schema: public; Owner: -
--

SELECT * FROM pg_catalog.pg_restore_relation_stats(
	'version', '150012'::integer,
	'schemaname', 'public',
	'relname', 'unique_idx_with_include_clause',
	'relpages', '0'::integer,
	'reltuples', '0'::real,
	'relallvisible', '0'::integer
);


-- YB: re-enable auto analyze after all catalog changes
DO $$
BEGIN
  IF EXISTS (SELECT 1 FROM pg_settings WHERE name = 'yb_disable_auto_analyze') THEN
    EXECUTE format('ALTER DATABASE %I SET yb_disable_auto_analyze TO off', current_database());
  END IF;
END $$;

--
-- YSQL database dump complete
--

