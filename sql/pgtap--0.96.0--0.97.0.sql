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
   SELECT ok ( _is_indexed( $1, ARRAY[$2]::NAME[]),
              'An index on ' || quote_ident($1) || ' on column '
                  || $2::text || ' should exist');
$$ LANGUAGE sql;

-- pg_version_num()
CREATE OR REPLACE FUNCTION pg_version_num()
RETURNS integer AS $$
    SELECT substring(s.a[1] FROM '[[:digit:]]+')::int * 10000
           + COALESCE(substring(s.a[2] FROM '[[:digit:]]+')::int, 0) * 100
           + COALESCE(substring(s.a[3] FROM '[[:digit:]]+')::int, 0)
      FROM (
          SELECT string_to_array(current_setting('server_version'), '.') AS a
      ) AS s;
$$ LANGUAGE SQL IMMUTABLE;


    -- isnt_definer( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_definer ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _definer($1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_definer( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_definer( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _definer($1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be not security definer'
    );
$$ LANGUAGE sql;

-- isnt_definer( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_definer ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _definer($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_definer( schema, function )
CREATE OR REPLACE FUNCTION isnt_definer( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _definer($1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should be not security definer'
    );
$$ LANGUAGE sql;

-- isnt_definer( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_definer ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _definer($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_definer( function, args[] )
CREATE OR REPLACE FUNCTION isnt_definer( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _definer($1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be not security definer'
    );
$$ LANGUAGE sql;

-- isnt_definer( function, description )
CREATE OR REPLACE FUNCTION isnt_definer( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _definer($1), $2 );
$$ LANGUAGE sql;

-- isnt_definer( function )
CREATE OR REPLACE FUNCTION isnt_definer( NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _definer($1), 'Function ' || quote_ident($1) || '() should be not security definer' );
$$ LANGUAGE sql;

-- isnt_aggregate( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _agg($1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_aggregate( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _agg($1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _agg($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_aggregate( schema, function )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _agg($1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _agg($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_aggregate( function, args[] )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _agg($1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( function, description )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _agg($1), $2 );
$$ LANGUAGE sql;

-- isnt_aggregate( function )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _agg($1), 'Function ' || quote_ident($1) || '() should not be an aggregate function' );
$$ LANGUAGE sql;
