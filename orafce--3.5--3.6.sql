CREATE FUNCTION oracle.get_major_version()
RETURNS text
AS 'MODULE_PATHNAME','ora_get_major_version'
LANGUAGE 'c' STRICT IMMUTABLE;

CREATE FUNCTION oracle.get_major_version_num()
RETURNS text
AS 'MODULE_PATHNAME','ora_get_major_version_num'
LANGUAGE 'c' STRICT IMMUTABLE;

CREATE FUNCTION oracle.get_full_version_num()
RETURNS text
AS 'MODULE_PATHNAME','ora_get_full_version_num'
LANGUAGE 'c' STRICT IMMUTABLE;

CREATE FUNCTION oracle.get_platform()
RETURNS text
AS 'MODULE_PATHNAME','ora_get_platform'
LANGUAGE 'c' STRICT IMMUTABLE;

CREATE FUNCTION oracle.get_status()
RETURNS text
AS 'MODULE_PATHNAME','ora_get_status'
LANGUAGE 'c' STRICT IMMUTABLE;

-- Oracle system views
create view oracle.user_tab_columns as
    select table_name,
           column_name,
           data_type,
           coalesce(character_maximum_length, numeric_precision) AS data_length,
           numeric_precision AS data_precision,
           numeric_scale AS data_scale,
           is_nullable AS nullable,
           ordinal_position AS column_id,
           is_updatable AS data_upgraded,
           table_schema
    from information_schema.columns;

create view oracle.user_tables as
    select table_name
      from information_schema.tables
     where table_type = 'BASE_TABLE';

create view oracle.user_cons_columns as
   select constraint_name, column_name, table_name
     from information_schema.constraint_column_usage ;

create view oracle.user_constraints as
    select conname as constraint_name,
           case contype when 'p' then 'P' when 'f' then 'R' end as constraint_type,
           relname as table_name,
           case contype when 'f' then (select conname
                                         from pg_constraint c2
                                        where contype = 'p' and c2.conindid = c1.conindid)
                                      end as r_constraint_name
      from pg_constraint c1, pg_class
     where conrelid = pg_class.oid;

create view oracle.product_component_version as
    select oracle.get_major_version() as product,
           oracle.get_full_version_num() as version,
           oracle.get_platform() || ' ' || oracle.get_status() as status
    union all
    select extname,
           case when extname = 'plpgsql' then oracle.get_full_version_num() else extversion end,
           oracle.get_platform() || ' ' || oracle.get_status()
      from pg_extension;

create view oracle.user_objects as
    select relname as object_name,
           null::text as subject_name,
           c.oid as object_id,
           case relkind when 'r' then 'TABLE'
                        when 'i' then 'INDEX'
                        when 'S' then 'SEQUENCE'
                        when 'v' then 'VIEW'
                        when 'm' then 'VIEW'
                        when 'f' then 'FOREIGN TABLE' end as object_type,
           null::timestamp(0) as created,
           null::timestamp(0) as last_ddl_time,
           case when relkind = 'i' then (select case when indisvalid then 'VALID' else 'INVALID' end
                                          from pg_index
                                         where indexrelid = c.oid)
                                   else case when relispopulated then 'VALID' else 'INVALID' end end as status,
           relnamespace as namespace
      from pg_class c join pg_namespace n on c.relnamespace = n.oid
     where relkind not in  ('t','c')
       and nspname not in ('pg_toast','pg_catalog','information_schema')
    union all
    select tgname, null, t.oid, 'TRIGGER',null, null,'VALID', relnamespace
      from pg_trigger t join pg_class c on t.tgrelid = c.oid
     where not tgisinternal
    union all
    select proname, null, p.oid, 'FUNCTION', null, null, 'VALID', pronamespace
      from pg_proc p join pg_namespace n on p.pronamespace = n.oid
     where nspname not in ('pg_toast','pg_catalog','information_schema') order by 1;

-- Oracle dirty functions
CREATE OR REPLACE FUNCTION oracle.lpad(int, int, int)
RETURNS text AS $$
SELECT pg_catalog.lpad($1::text,$2,$3::text)
$$ LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.lpad(bigint, int, int)
RETURNS text AS $$
SELECT pg_catalog.lpad($1::text,$2,$3::text)
$$ LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.lpad(smallint, int, int)
RETURNS text AS $$
SELECT pg_catalog.lpad($1::text,$2,$3::text)
$$ LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.lpad(numeric, int, int)
RETURNS text AS $$
SELECT pg_catalog.lpad($1::text,$2,$3::text)
$$ LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.nvl(bigint, int)
RETURNS bigint AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION oracle.nvl(numeric, int)
RETURNS numeric AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE STRICT;
