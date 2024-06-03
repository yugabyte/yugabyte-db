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
     where table_type = 'BASE TABLE';

create view oracle.user_cons_columns as
   select constraint_name, column_name, table_name
     from information_schema.constraint_column_usage ;

create view oracle.user_constraints as
    select conname as constraint_name,
           conindid::regclass as index_name,
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

create view oracle.user_procedures as
    select proname as object_name
      from pg_proc p join pg_namespace n on p.pronamespace = n.oid
       and nspname <> 'pg_catalog';

create view oracle.user_source as
    select row_number() over (partition by oid) as line, *
      from ( select oid, unnest(string_to_array(prosrc, e'\n')) as text,
                    proname as name, 'FUNCTION'::text as type
               from pg_proc) s;

create view oracle.user_views
   as select c.relname as view_name,
  pg_catalog.pg_get_userbyid(c.relowner) as owner
from pg_catalog.pg_class c
     left join pg_catalog.pg_namespace n on n.oid = c.relnamespace
where c.relkind in ('v','')
      and n.nspname <> 'pg_catalog'
      and n.nspname <> 'information_schema'
      and n.nspname !~ '^pg_toast'
  and pg_catalog.pg_table_is_visible(c.oid);

create view oracle.user_ind_columns as
    select attname as column_name, c1.relname as index_name, c2.relname as table_name
      from (select unnest(indkey) attno, indexrelid, indrelid from pg_index) s
           join pg_attribute on attno = attnum and attrelid = indrelid
           join pg_class c1 on indexrelid = c1.oid
           join pg_class c2 on indrelid = c2.oid
           join pg_namespace n on c2.relnamespace = n.oid
     where attno > 0 and nspname not in ('pg_catalog','information_schema');

CREATE VIEW oracle.dba_segments AS
SELECT
    pg_namespace.nspname AS owner,
    pg_class.relname AS segment_name,
    CASE
        WHEN pg_class.relkind = 'r' THEN CAST( 'TABLE' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 'i' THEN CAST( 'INDEX' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 'f' THEN CAST( 'FOREIGN TABLE' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 'S' THEN CAST( 'SEQUENCE' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 's' THEN CAST( 'SPECIAL' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 't' THEN CAST( 'TOAST TABLE' AS VARCHAR( 18 ) )
        WHEN pg_class.relkind = 'v' THEN CAST( 'VIEW' AS VARCHAR( 18 ) )
        ELSE CAST( pg_class.relkind AS VARCHAR( 18 ) )
    END AS segment_type,
    spcname AS tablespace_name,
    relfilenode AS header_file,
    NULL::oid AS header_block,
    pg_relation_size( pg_class.oid ) AS bytes,
    relpages AS blocks
FROM
    pg_class
    INNER JOIN pg_namespace
     ON pg_class.relnamespace = pg_namespace.oid
    LEFT OUTER JOIN pg_tablespace
     ON pg_class.reltablespace = pg_tablespace.oid
WHERE
    pg_class.relkind not in ('f','S','v');

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
$$ LANGUAGE sql IMMUTABLE;

CREATE OR REPLACE FUNCTION oracle.nvl(numeric, int)
RETURNS numeric AS $$
SELECT coalesce($1, $2)
$$ LANGUAGE sql IMMUTABLE;
