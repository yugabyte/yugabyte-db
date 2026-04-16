CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.97;'
LANGUAGE SQL IMMUTABLE;

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

CREATE OR REPLACE FUNCTION _is_indexed( NAME, NAME, TEXT[] )
RETURNS BOOL AS $$
SELECT EXISTS( SELECT TRUE FROM (
        SELECT _ikeys(coalesce($1, n.nspname), $2, ci.relname) AS cols
          FROM pg_catalog.pg_index x
          JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
          JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
          JOIN pg_catalog.pg_namespace n ON n.oid = ct.relnamespace
         WHERE ($1 IS NULL OR n.nspname  = $1)
           AND ct.relname = $2
    ) icols
    WHERE cols = $3 )
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
   SELECT ok( _is_indexed(NULL, $1, $2), $3 );
$$ LANGUAGE sql;

-- is_indexed( table, columns[] )
CREATE OR REPLACE FUNCTION is_indexed ( NAME, NAME[] )
RETURNS TEXT AS $$
   SELECT ok(
       _is_indexed(NULL, $1, $2),
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
   SELECT ok ( _is_indexed( NULL, $1, ARRAY[$2]::NAME[]) );
$$ LANGUAGE sql;


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
        array_to_string($3, ', ') || ') should not be security definer'
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
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be security definer'
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
        array_to_string($2, ', ') || ') should not be security definer'
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
    SELECT ok( NOT _definer($1), 'Function ' || quote_ident($1) || '() should not be security definer' );
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


-- https://github.com/theory/pgtap/pull/99

-- hasnt_opclass( schema, name )
CREATE OR REPLACE FUNCTION hasnt_opclass( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _opc_exists( $1, $2 ), 'Operator class ' || quote_ident($1) || '.' || quote_ident($2) || ' should not exist' );
$$ LANGUAGE SQL;

-- hasnt_opclass( name )
CREATE OR REPLACE FUNCTION hasnt_opclass( NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _opc_exists( $1 ), 'Operator class ' || quote_ident($1) || ' should not exist' );
$$ LANGUAGE SQL;

-- https://github.com/theory/pgtap/pull/101

-- check extension exists function with schema name
CREATE OR REPLACE FUNCTION _ext_exists( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
        SELECT TRUE
          FROM pg_catalog.pg_extension ex
          JOIN pg_catalog.pg_namespace n ON ex.extnamespace = n.oid
         WHERE n.nspname  = $1
           AND ex.extname = $2
    );
$$ LANGUAGE SQL;

-- check extension exists function without schema name
CREATE OR REPLACE FUNCTION _ext_exists( NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
        SELECT TRUE
          FROM pg_catalog.pg_extension ex
         WHERE ex.extname = $1
    );
$$ LANGUAGE SQL;

-- has_extension( schema, name, description )
CREATE OR REPLACE FUNCTION has_extension( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ext_exists( $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_extension( schema, name )
CREATE OR REPLACE FUNCTION has_extension( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ext_exists( $1, $2 ),
        'Extension ' || quote_ident($2)
        || ' should exist in schema ' || quote_ident($1) );
$$ LANGUAGE SQL;

-- has_extension( name, description )
CREATE OR REPLACE FUNCTION has_extension( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _ext_exists( $1 ), $2)
$$ LANGUAGE SQL;

-- has_extension( name )
CREATE OR REPLACE FUNCTION has_extension( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _ext_exists( $1 ),
        'Extension ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_extension( schema, name, description )
CREATE OR REPLACE FUNCTION hasnt_extension( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _ext_exists( $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_extension( schema, name )
CREATE OR REPLACE FUNCTION hasnt_extension( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _ext_exists( $1, $2 ),
        'Extension ' || quote_ident($2)
        || ' should not exist in schema ' || quote_ident($1) );
$$ LANGUAGE SQL;

-- hasnt_extension( name, description )
CREATE OR REPLACE FUNCTION hasnt_extension( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _ext_exists( $1 ), $2)
$$ LANGUAGE SQL;

-- hasnt_extension( name )
CREATE OR REPLACE FUNCTION hasnt_extension( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _ext_exists( $1 ),
        'Extension ' || quote_ident($1) || ' should not exist' );
$$ LANGUAGE SQL;

-- https://github.com/theory/pgtap/pull/119

-- throws_ok ( sql, errcode, errmsg, description )
CREATE OR REPLACE FUNCTION throws_ok ( TEXT, CHAR(5), TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    query     TEXT := _query($1);
    errcode   ALIAS FOR $2;
    errmsg    ALIAS FOR $3;
    desctext  ALIAS FOR $4;
    descr     TEXT;
BEGIN
    descr := COALESCE(
          desctext,
          'threw ' || errcode || ': ' || errmsg,
          'threw ' || errcode,
          'threw ' || errmsg,
          'threw an exception'
    );
    EXECUTE query;
    RETURN ok( FALSE, descr ) || E'\n' || diag(
           '      caught: no exception' ||
        E'\n      wanted: ' || COALESCE( errcode, 'an exception' )
    );
EXCEPTION WHEN OTHERS OR ASSERT_FAILURE THEN
    IF (errcode IS NULL OR SQLSTATE = errcode)
        AND ( errmsg IS NULL OR SQLERRM = errmsg)
    THEN
        -- The expected errcode and/or message was thrown.
        RETURN ok( TRUE, descr );
    ELSE
        -- This was not the expected errcode or errmsg.
        RETURN ok( FALSE, descr ) || E'\n' || diag(
               '      caught: ' || SQLSTATE || ': ' || SQLERRM ||
            E'\n      wanted: ' || COALESCE( errcode, 'an exception' ) ||
            COALESCE( ': ' || errmsg, '')
        );
    END IF;
END;
$$ LANGUAGE plpgsql;

-- lives_ok( sql, description )
CREATE OR REPLACE FUNCTION lives_ok ( TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    code  TEXT := _query($1);
    descr ALIAS FOR $2;
    detail  text;
    hint    text;
    context text;
    schname text;
    tabname text;
    colname text;
    chkname text;
    typname text;
BEGIN
    EXECUTE code;
    RETURN ok( TRUE, descr );
EXCEPTION WHEN OTHERS OR ASSERT_FAILURE THEN
    -- There should have been no exception.
    GET STACKED DIAGNOSTICS
        detail  = PG_EXCEPTION_DETAIL,
        hint    = PG_EXCEPTION_HINT,
        context = PG_EXCEPTION_CONTEXT,
        schname = SCHEMA_NAME,
        tabname = TABLE_NAME,
        colname = COLUMN_NAME,
        chkname = CONSTRAINT_NAME,
        typname = PG_DATATYPE_NAME;
    RETURN ok( FALSE, descr ) || E'\n' || diag(
           '    died: ' || _error_diag(SQLSTATE, SQLERRM, detail, hint, context, schname, tabname, colname, chkname, typname)
    );
END;
$$ LANGUAGE plpgsql;

