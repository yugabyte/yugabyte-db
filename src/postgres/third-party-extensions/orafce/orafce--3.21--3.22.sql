--
-- move objects from pg_catalog and from public schema to schema oracle
--

DO $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 120000) THEN
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(date,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(date,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.next_day(date,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.next_day(date,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.last_day(date) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.months_between(date,date) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.add_months(date,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(timestamp with time zone,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(timestamp with time zone,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(timestamp with time zone) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(date) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(timestamp with time zone) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(date) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.nlssort(text,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.nlssort(text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.set_nls_sort(text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.instr(text,text,integer,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.instr(text,text,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.instr(text,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(smallint) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(bigint) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(real) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(double precision) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_char(numeric) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_number(text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_number(numeric) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_number(numeric,numeric) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.lnnvl(boolean) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.listagg1_transfn(internal,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.wm_concat_transfn(internal,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.listagg2_transfn(internal,text,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.listagg_finalfn(internal) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.listagg(text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.wm_concat(text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.listagg(text,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median4_transfn(internal,real) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median4_finalfn(internal) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median8_transfn(internal,double precision) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median8_finalfn(internal) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median(real) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.median(double precision) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.substrb(varchar2,integer,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.substrb(varchar2,integer) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.lengthb(varchar2) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.strposb(varchar2,varchar2) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(timestamp without time zone,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(timestamp without time zone,text) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.round(timestamp without time zone) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.trunc(timestamp without time zone) SET SCHEMA oracle$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.to_date(text) RENAME TO orafce__obsolete_to_date$_$;
    EXECUTE $_$ALTER FUNCTION pg_catalog.orafce__obsolete_to_date(text) SET SCHEMA oracle$_$;
  ELSE
    -- Pre PostgreSQL 12 doesn't allow ALTER FUNCTION pg_catalog.xx SET SCHEMA
    -- So we need to use dirty way
    ALTER FUNCTION pg_catalog.to_date(text) RENAME TO orafce__obsolete_to_date;

    INSERT INTO pg_depend
      SELECT 'pg_proc'::regclass, oid, 0, 'pg_namespace'::regclass, 'oracle'::regnamespace, 0, 'n'
          FROM pg_proc WHERE oid IN (SELECT objid
                                       FROM pg_depend
                                      WHERE refclassid = 'pg_extension'::regclass AND refobjid = (SELECT oid
                                                                                                    FROM pg_extension
                                                                                                   WHERE extname = 'orafce')
                                        AND classid = 'pg_proc'::regclass)
                         AND pronamespace = 'pg_catalog'::regnamespace;

    UPDATE pg_proc
       SET pronamespace = 'oracle'::regnamespace
     WHERE oid  IN (SELECT objid
                      FROM pg_depend
                     WHERE refclassid = 'pg_extension'::regclass AND refobjid = (SELECT oid
                                                                                   FROM pg_extension
                                                                                  WHERE extname = 'orafce')
                       AND classid = 'pg_proc'::regclass)
      AND pronamespace = 'pg_catalog'::regnamespace;
  END IF;
END;
$$;

ALTER FUNCTION public.to_multi_byte(text) SET SCHEMA oracle;
ALTER FUNCTION public.to_single_byte(text) SET SCHEMA oracle;
ALTER FUNCTION public.bitand(bigint,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.sinh(double precision) SET SCHEMA oracle;
ALTER FUNCTION public.cosh(double precision) SET SCHEMA oracle;
ALTER FUNCTION public.tanh(double precision) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(real,real) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(double precision,double precision) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(numeric,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(real,character varying) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(double precision,character varying) SET SCHEMA oracle;
ALTER FUNCTION public.nanvl(numeric,character varying) SET SCHEMA oracle;
ALTER FUNCTION public.dump("any") SET SCHEMA oracle;
ALTER FUNCTION public.dump("any",integer) SET SCHEMA oracle;
ALTER FUNCTION public.nvl(anyelement,anyelement) SET SCHEMA oracle;
ALTER FUNCTION public.nvl2("any",anyelement,anyelement) SET SCHEMA oracle;
ALTER FUNCTION public.nvl2("any",text,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text,anyelement,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text,anyelement,text,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text,anyelement,text,anyelement,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,text,anyelement,text,anyelement,text,text) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character,anyelement,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character,anyelement,character,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character,anyelement,character,anyelement,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,character,anyelement,character,anyelement,character,character) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer,anyelement,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer,anyelement,integer,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer,anyelement,integer,anyelement,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,integer,anyelement,integer,anyelement,integer,integer) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint,anyelement,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint,anyelement,bigint,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint,anyelement,bigint,anyelement,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,bigint,anyelement,bigint,anyelement,bigint,bigint) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric,anyelement,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric,anyelement,numeric,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric,anyelement,numeric,anyelement,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,numeric,anyelement,numeric,anyelement,numeric,numeric) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date,anyelement,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date,anyelement,date,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date,anyelement,date,anyelement,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,date,anyelement,date,anyelement,date,date) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone,anyelement,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone,anyelement,time without time zone,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone,anyelement,time without time zone,anyelement,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,time without time zone,anyelement,time without time zone,anyelement,time without time zone,time without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone,anyelement,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone,anyelement,timestamp without time zone,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone,anyelement,timestamp without time zone,anyelement,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp without time zone,anyelement,timestamp without time zone,anyelement,timestamp without time zone,timestamp without time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone,anyelement,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone,anyelement,timestamp with time zone,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone,anyelement,timestamp with time zone,anyelement,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.decode(anyelement,anyelement,timestamp with time zone,anyelement,timestamp with time zone,anyelement,timestamp with time zone,timestamp with time zone) SET SCHEMA oracle;
ALTER FUNCTION public.dump(text) SET SCHEMA oracle;
ALTER FUNCTION public.dump(text,integer) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2in(cstring,oid,integer) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2out(varchar2) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2_transform(internal) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2recv(internal,oid,integer) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2send(varchar2) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2typmodin(cstring[]) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2typmodout(integer) SET SCHEMA oracle;
ALTER FUNCTION public.varchar2(varchar2,integer,boolean) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2in(cstring,oid,integer) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2out(nvarchar2) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2_transform(internal) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2recv(internal,oid,integer) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2send(nvarchar2) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2typmodin(cstring[]) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2typmodout(integer) SET SCHEMA oracle;
ALTER FUNCTION public.nvarchar2(nvarchar2,integer,boolean) SET SCHEMA oracle;

ALTER TYPE public.nvarchar2 SET SCHEMA oracle;
ALTER TYPE public.varchar2 SET SCHEMA oracle;


ALTER VIEW public.dual SET SCHEMA oracle;

ALTER OPERATOR || (oracle.nvarchar2, oracle.nvarchar2) SET SCHEMA oracle;
ALTER OPERATOR || (oracle.varchar2, oracle.varchar2) SET SCHEMA oracle;

CREATE OR REPLACE FUNCTION oracle.to_number(numeric)
RETURNS numeric AS $$
SELECT oracle.to_number($1::text);
$$ LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.to_number(numeric,numeric)
RETURNS numeric AS $$
SELECT pg_catalog.to_number($1::text,$2::text);
$$ LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.sinh(float8)
RETURNS float8 AS
$$ SELECT (pg_catalog.exp($1) - pg_catalog.exp(-$1)) / 2; $$
LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.cosh(float8)
RETURNS float8 AS
$$ SELECT (pg_catalog.exp($1) + pg_catalog.exp(-$1)) / 2; $$
LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.tanh(float8)
RETURNS float8 AS
$$ SELECT oracle.sinh($1) / oracle.cosh($1); $$
LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.add_months(TIMESTAMP WITH TIME ZONE,INTEGER)
RETURNS TIMESTAMP
AS $$ SELECT (oracle.add_months($1::pg_catalog.date, $2) + $1::time)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.last_day(TIMESTAMPTZ)
RETURNS TIMESTAMP
AS $$ SELECT (pg_catalog.date_trunc('MONTH', $1) + INTERVAL '1 MONTH - 1 day' + $1::time)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.months_between(TIMESTAMP WITH TIME ZONE,TIMESTAMP WITH TIME ZONE)
RETURNS NUMERIC
AS $$ SELECT oracle.months_between($1::pg_catalog.date,$2::pg_catalog.date); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.next_day(TIMESTAMP WITH TIME ZONE,INTEGER)
RETURNS TIMESTAMP
AS $$ SELECT (oracle.next_day($1::pg_catalog.date,$2) + $1::time)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.next_day(TIMESTAMP WITH TIME ZONE,TEXT)
RETURNS TIMESTAMP
AS $$ SELECT (oracle.next_day($1::pg_catalog.date,$2) + $1::time)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.to_date(TEXT)
RETURNS oracle.date
AS $$ SELECT oracle.orafce__obsolete_to_date($1)::oracle.date; $$
LANGUAGE SQL STABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.to_date(TEXT,TEXT)
RETURNS oracle.date
AS $$ SELECT TO_TIMESTAMP($1,$2)::oracle.date; $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.nlssort(text)
RETURNS bytea
AS $$ SELECT oracle.nlssort($1, null); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.round(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT oracle.round($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.trunc(value timestamp with time zone)
RETURNS timestamp with time zone
AS $$ SELECT oracle.trunc($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.round(value timestamp without time zone)
RETURNS timestamp without time zone
AS $$ SELECT oracle.round($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.trunc(value timestamp without time zone)
RETURNS timestamp without time zone
AS $$ SELECT oracle.trunc($1, 'DDD'); $$
LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.mod(SMALLINT, SMALLINT)
RETURNS SMALLINT AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(INT, INT)
RETURNS INT AS $$
   SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE SQL  IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(BIGINT, BIGINT)
RETURNS BIGINT AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE sql IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.mod(NUMERIC, NUMERIC)
RETURNS NUMERIC AS $$
SELECT CASE $2 WHEN 0 THEN $1 ELSE pg_catalog.MOD($1, $2) END;
$$ LANGUAGE sql IMMUTABLE;
