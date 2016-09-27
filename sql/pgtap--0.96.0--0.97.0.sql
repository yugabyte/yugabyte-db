CREATE OR REPLACE FUNCTION _is_indexed( NAME, NAME, TEXT[] )
RETURNS BOOL AS $$
    WITH icols AS (
        SELECT _ikeys($1, $2, ci.relname) AS cols
          FROM pg_catalog.pg_index x
          JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
          JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
          JOIN pg_catalog.pg_namespace n ON n.oid = ct.relnamespace
         WHERE n.nspname  = $1
           AND ct.relname = $2
    ) SELECT EXISTS( SELECT TRUE from icols WHERE cols = $3 )
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _is_indexed( NAME, TEXT[] )
RETURNS BOOL AS $$
    WITH icols AS (
        SELECT _ikeys($1, ci.relname) AS cols
          FROM pg_catalog.pg_index x
          JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
          JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
         WHERE ct.relname = $1
           AND pg_catalog.pg_table_is_visible(ct.oid)
    ) SELECT EXISTS( SELECT TRUE from icols WHERE cols = $2 )
$$ LANGUAGE sql;

-- is_indexed( schema, table, columns[], description )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
   SELECT ok( _is_indexed($1, $2, $3), $4 );
$$ LANGUAGE sql;

-- is_indexed( schema, table, columns[] )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
   SELECT ok(
       _is_indexed($1, $2, $3),
       'Should have an index on ' ||  quote_ident($1) || '.' || quote_ident($2) || '(' || array_to_string( $3, ', ' ) || ')'
    );
$$ LANGUAGE sql;

-- is_indexed( table, columns[], description )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
   SELECT ok( _is_indexed($1, $2), $3 );
$$ LANGUAGE sql;

-- is_indexed( table, columns[] )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME[] )
RETURNS TEXT AS $$
   SELECT ok(
       _is_indexed($1, $2),
       'Should have an index on ' ||  quote_ident($1) || '(' || array_to_string( $2, ', ' ) || ')'
   );
$$ LANGUAGE sql;

-- is_indexed( schema, table, column, description )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
   SELECT ok ( _is_indexed( $1, $2, ARRAY[$3]::NAME[]), $4);
$$ LANGUAGE sql;

-- is_indexed( schema, table, column )
-- is_indexed( table, column, description )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT CASE WHEN _is_schema( $1 ) THEN
                -- Looking for schema.table index.
                is_indexed( $1, $2, ARRAY[$3]::NAME[] )
           ELSE
                -- Looking for particular columns.
                is_indexed( $1, ARRAY[$2]::NAME[], $3 )
           END;
$$ LANGUAGE sql;

-- is_indexed( table, column )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME )
RETURNS TEXT AS $$
   SELECT ok ( _is_indexed( $1, ARRAY[$2]::NAME[]) );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION pg_version_num()
RETURNS integer AS $$
    SELECT substring(s.a[1] FROM '[[:digit:]]+')::int * 10000
           + COALESCE(substring(s.a[2] FROM '[[:digit:]]+')::int, 0) * 100
           + COALESCE(substring(s.a[3] FROM '[[:digit:]]+')::int, 0)
      FROM (
          SELECT string_to_array(current_setting('server_version'), '.') AS a
      ) AS s;
$$ LANGUAGE SQL IMMUTABLE;
