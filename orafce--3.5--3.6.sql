
-- Oracle system views
create view user_tab_columns as
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

create view user_tables as
    select table_name
      from information_schema.tables
     where table_type = 'BASE_TABLE';

create view user_cons_columns as
   select constraint_name, column_name, table_name
     from information_schema.constraint_column_usage ;

create view user_constraints as
    select conname as constraint_name,
           case contype when 'p' then 'P' when 'f' then 'R' end as constraint_type,
           relname as table_name,
           case contype when 'f' then (select conname
                                         from pg_constraint c2
                                        where contype = 'p' and c2.conindid = c1.conindid)
                                      end as r_constraint_name
      from pg_constraint c1, pg_class
     where conrelid = pg_class.oid;

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
