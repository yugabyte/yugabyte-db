--
-- CREATE_TABLE
--

-- Error cases
-- TODO(jason): uncomment when issue #1975 is closed or closing.
-- CREATE TABLE unknowntab (
-- 	u unknown    -- fail
-- );

CREATE TYPE unknown_comptype AS (
	u unknown    -- fail
);

-- invalid: non-lowercase quoted reloptions identifiers
CREATE TABLE tas_case WITH ("Fillfactor" = 10) AS SELECT 1 a;
CREATE TABLE tas_case (a text) WITH ("Oids" = true);

CREATE UNLOGGED TABLE unlogged1 (a int primary key);			-- OK
CREATE TEMPORARY TABLE unlogged2 (a int primary key);			-- OK
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
REINDEX INDEX unlogged2_pkey;
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
DROP TABLE unlogged2;
CREATE TABLE pg_temp.implicitly_temp (a int primary key);		-- OK
CREATE TEMP TABLE explicitly_temp (a int primary key);			-- also OK
CREATE TEMP TABLE pg_temp.doubly_temp (a int primary key);		-- also OK
CREATE TEMP TABLE public.temp_to_perm (a int primary key);		-- not OK
