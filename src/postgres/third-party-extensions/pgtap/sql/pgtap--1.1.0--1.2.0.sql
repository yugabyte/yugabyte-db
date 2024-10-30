CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 1.2;'
LANGUAGE SQL IMMUTABLE;

-- isnt_member_of( role, members[], description )
CREATE OR REPLACE FUNCTION isnt_member_of( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    extra text[];
BEGIN
    IF NOT _has_role($1) THEN
        RETURN fail( $3 ) || E'\n' || diag (
            '    Role ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    SELECT ARRAY(
        SELECT quote_ident($2[i])
          FROM generate_series(1, array_upper($2, 1)) s(i)
          LEFT JOIN pg_catalog.pg_roles r ON rolname = $2[i]
         WHERE r.oid = ANY ( _grolist($1) )
         ORDER BY s.i
    ) INTO extra;
    IF extra[1] IS NULL THEN
        RETURN ok( true, $3 );
    END IF;
    RETURN ok( false, $3 ) || E'\n' || diag(
        '    Members, who should not be in ' || quote_ident($1) || E' role:\n        ' ||
        array_to_string( extra, E'\n        ')
    );
END;
$$ LANGUAGE plpgsql;

-- isnt_member_of( role, member, description )
CREATE OR REPLACE FUNCTION isnt_member_of( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT isnt_member_of( $1, ARRAY[$2], $3 );
$$ LANGUAGE SQL;

-- isnt_member_of( role, members[] )
CREATE OR REPLACE FUNCTION isnt_member_of( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT isnt_member_of( $1, $2, 'Should not have members of role ' || quote_ident($1) );
$$ LANGUAGE SQL;

-- isnt_member_of( role, member )
CREATE OR REPLACE FUNCTION isnt_member_of( NAME, NAME )
RETURNS TEXT AS $$
    SELECT isnt_member_of( $1, ARRAY[$2] );
$$ LANGUAGE SQL;

-- has_view( schema, view )
CREATE OR REPLACE FUNCTION has_view ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT has_view ($1, $2, 
    'View ' || quote_ident($1) || '.' || quote_ident($2) || ' should exist'
    );    
$$ LANGUAGE SQL;

-- hasnt_view( schema, table )
CREATE OR REPLACE FUNCTION hasnt_view ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT hasnt_view( $1, $2,
        'View ' || quote_ident($1) || '.' || quote_ident($2) || ' should not exist'
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _def_is( TEXT, TEXT, anyelement, TEXT )
RETURNS TEXT AS $$
DECLARE
    thing text;
BEGIN
    -- Function, cast, or special SQL syntax.
    IF $1 ~ '^[^'']+[(]' OR $1 ~ '[)]::[^'']+$' OR $1 = ANY('{CURRENT_CATALOG,CURRENT_ROLE,CURRENT_SCHEMA,CURRENT_USER,SESSION_USER,USER,CURRENT_DATE,CURRENT_TIME,CURRENT_TIMESTAMP,LOCALTIME,LOCALTIMESTAMP}') THEN
        RETURN is( $1, $3, $4 );
    END IF;

    EXECUTE 'SELECT is('
             || COALESCE($1, 'NULL' || '::' || $2) || '::' || $2 || ', '
             || COALESCE(quote_literal($3), 'NULL') || '::' || $2 || ', '
             || COALESCE(quote_literal($4), 'NULL')
    || ')' INTO thing;
    RETURN thing;
END;
$$ LANGUAGE plpgsql;

-- results_eq( cursor, cursor, description )
CREATE OR REPLACE FUNCTION results_eq( refcursor, refcursor, text )
RETURNS TEXT AS $$
DECLARE
    have       ALIAS FOR $1;
    want       ALIAS FOR $2;
    have_rec   RECORD;
    want_rec   RECORD;
    have_found BOOLEAN;
    want_found BOOLEAN;
    rownum     INTEGER := 1;
    err_msg    text := 'details not available in pg <= 9.1';
BEGIN
    FETCH have INTO have_rec;
    have_found := FOUND;
    FETCH want INTO want_rec;
    want_found := FOUND;
    WHILE have_found OR want_found LOOP
        IF have_rec IS DISTINCT FROM want_rec OR have_found <> want_found THEN
            RETURN ok( false, $3 ) || E'\n' || diag(
                '    Results differ beginning at row ' || rownum || E':\n' ||
                '        have: ' || CASE WHEN have_found THEN have_rec::text ELSE 'NULL' END || E'\n' ||
                '        want: ' || CASE WHEN want_found THEN want_rec::text ELSE 'NULL' END
            );
        END IF;
        rownum = rownum + 1;
        FETCH have INTO have_rec;
        have_found := FOUND;
        FETCH want INTO want_rec;
        want_found := FOUND;
    END LOOP;

    RETURN ok( true, $3 );
EXCEPTION
    WHEN datatype_mismatch THEN
        GET STACKED DIAGNOSTICS err_msg = MESSAGE_TEXT;
        RETURN ok( false, $3 ) || E'\n' || diag(
            E'    Number of columns or their types differ between the queries' ||
            CASE WHEN have_rec::TEXT = want_rec::text THEN '' ELSE E':\n' ||
                '        have: ' || CASE WHEN have_found THEN have_rec::text ELSE 'NULL' END || E'\n' ||
                '        want: ' || CASE WHEN want_found THEN want_rec::text ELSE 'NULL' END
            END || E'\n        ERROR: ' || err_msg
        );
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION results_ne( refcursor, refcursor, text )
RETURNS TEXT AS $$
DECLARE
    have       ALIAS FOR $1;
    want       ALIAS FOR $2;
    have_rec   RECORD;
    want_rec   RECORD;
    have_found BOOLEAN;
    want_found BOOLEAN;
    err_msg    text := 'details not available in pg <= 9.1';
BEGIN
    FETCH have INTO have_rec;
    have_found := FOUND;
    FETCH want INTO want_rec;
    want_found := FOUND;
    WHILE have_found OR want_found LOOP
        IF have_rec IS DISTINCT FROM want_rec OR have_found <> want_found THEN
            RETURN ok( true, $3 );
        ELSE
            FETCH have INTO have_rec;
            have_found := FOUND;
            FETCH want INTO want_rec;
            want_found := FOUND;
        END IF;
    END LOOP;
    RETURN ok( false, $3 );
EXCEPTION
    WHEN datatype_mismatch THEN
        GET STACKED DIAGNOSTICS err_msg = MESSAGE_TEXT;
        RETURN ok( false, $3 ) || E'\n' || diag(
            E'    Number of columns or their types differ between the queries' ||
            CASE WHEN have_rec::TEXT = want_rec::text THEN '' ELSE E':\n' ||
                '        have: ' || CASE WHEN have_found THEN have_rec::text ELSE 'NULL' END || E'\n' ||
                '        want: ' || CASE WHEN want_found THEN want_rec::text ELSE 'NULL' END
            END || E'\n        ERROR: ' || err_msg
        );
END;
$$ LANGUAGE plpgsql;

-- hasnt_operator( left_type, schema, name, right_type, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists($1, $2, $3, $4, $5 ), $6 );
$$ LANGUAGE SQL;

-- hasnt_operator( left_type, schema, name, right_type, return_type )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, $3, $4, $5 ),
        'Operator ' || quote_ident($2) || '.' || $3 || '(' || $1 || ',' || $4
        || ') RETURNS ' || $5 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_operator( left_type, name, right_type, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists($1, $2, $3, $4 ), $5 );
$$ LANGUAGE SQL;

-- hasnt_operator( left_type, name, right_type, return_type )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, $3, $4 ),
        'Operator ' ||  $2 || '(' || $1 || ',' || $3
        || ') RETURNS ' || $4 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_operator( left_type, name, right_type, description )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists($1, $2, $3 ), $4 );
$$ LANGUAGE SQL;

-- hasnt_operator( left_type, name, right_type )
CREATE OR REPLACE FUNCTION hasnt_operator ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, $3 ),
        'Operator ' ||  $2 || '(' || $1 || ',' || $3
        || ') should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_leftop( schema, name, right_type, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists(NULL, $1, $2, $3, $4), $5 );
$$ LANGUAGE SQL;

-- hasnt_leftop( schema, name, right_type, return_type )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists(NULL, $1, $2, $3, $4 ),
        'Left operator ' || quote_ident($1) || '.' || $2 || '(NONE,'
        || $3 || ') RETURNS ' || $4 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_leftop( name, right_type, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists(NULL, $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- hasnt_leftop( name, right_type, return_type )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists(NULL, $1, $2, $3 ),
        'Left operator ' || $1 || '(NONE,' || $2 || ') RETURNS ' || $3 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_leftop( name, right_type, description )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists(NULL, $1, $2), $3 );
$$ LANGUAGE SQL;

-- hasnt_leftop( name, right_type )
CREATE OR REPLACE FUNCTION hasnt_leftop ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists(NULL, $1, $2 ),
        'Left operator ' || $1 || '(NONE,' || $2 || ') should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, schema, name, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists( $1, $2, $3, NULL, $4), $5 );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, schema, name, return_type )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, $3, NULL, $4 ),
        'Right operator ' || quote_ident($2) || '.' || $3 || '('
        || $1 || ',NONE) RETURNS ' || $4 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, name, return_type, description )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists( $1, $2, NULL, $3), $4 );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, name, return_type )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, NULL, $3 ),
        'Right operator ' || $2 || '('
        || $1 || ',NONE) RETURNS ' || $3 || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, name, description )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _op_exists( $1, $2, NULL), $3 );
$$ LANGUAGE SQL;

-- hasnt_rightop( left_type, name )
CREATE OR REPLACE FUNCTION hasnt_rightop ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
         NOT _op_exists($1, $2, NULL ),
        'Right operator ' || $2 || '(' || $1 || ',NONE) should not exist'
    );
$$ LANGUAGE SQL;

/*
 * tap_funky used to just be a simple view, but the problem with that is the
 * definition of pg_proc changed in version 11. Thanks to how pg_dump (and
 * hence pg_upgrade) works, this made it impossible to upgrade Postgres if
 * pgTap was installed. In order to fix that, we need code that will actually
 * work on both < PG11 and >= PG11.
 */
CREATE OR REPLACE FUNCTION _prokind( p_oid oid )
RETURNS "char" AS $$
BEGIN
    IF pg_version_num() >= 110000 THEN
        RETURN prokind FROM pg_catalog.pg_proc WHERE oid = p_oid;
    ELSE
        RETURN CASE WHEN proisagg THEN 'a' WHEN proiswindow THEN 'w' ELSE 'f' END
            FROM pg_catalog.pg_proc WHERE oid = p_oid;
    END IF;
END;
$$ LANGUAGE plpgsql STABLE;

-- Returns true if the specified function exists and is the specified type,
-- false if it exists and is not the specified type, and NULL if it does not
-- exist. Types are f for a normal function, p for a procedure, a for an
-- aggregate function, or w for a window function
-- _type_func(type, schema, function, args[])
CREATE OR REPLACE FUNCTION _type_func ( "char", NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = $1
      FROM tap_funky
     WHERE schema = $2
       AND name   = $3
       AND args   = array_to_string($4, ',')
$$ LANGUAGE SQL;

-- _type_func(type, schema, function)
CREATE OR REPLACE FUNCTION _type_func ( "char", NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT kind = $1 FROM tap_funky WHERE schema = $2 AND name = $3
$$ LANGUAGE SQL;

-- _type_func(type, function, args[])
CREATE OR REPLACE FUNCTION _type_func ( "char", NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = $1
      FROM tap_funky
     WHERE name = $2
       AND args = array_to_string($3, ',')
       AND is_visible;
$$ LANGUAGE SQL;

-- _type_func(type, function)
CREATE OR REPLACE FUNCTION _type_func ( "char", NAME )
RETURNS BOOLEAN AS $$
    SELECT kind = $1 FROM tap_funky WHERE name = $2 AND is_visible;
$$ LANGUAGE SQL;

-- is_aggregate( schema, function, args[], description )
CREATE OR REPLACE FUNCTION is_aggregate ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, _type_func( 'a', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- is_aggregate( schema, function, args[] )
CREATE OR REPLACE FUNCTION is_aggregate( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, _type_func('a', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be an aggregate function'
    );
$$ LANGUAGE sql;

-- is_aggregate( schema, function, description )
CREATE OR REPLACE FUNCTION is_aggregate ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, _type_func('a', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_aggregate( schema, function )
CREATE OR REPLACE FUNCTION is_aggregate( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, _type_func('a', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should be an aggregate function'
    );
$$ LANGUAGE sql;

-- is_aggregate( function, args[], description )
CREATE OR REPLACE FUNCTION is_aggregate ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare( NULL, $1, $2, _type_func('a', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_aggregate( function, args[] )
CREATE OR REPLACE FUNCTION is_aggregate( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, _type_func('a', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be an aggregate function'
    );
$$ LANGUAGE sql;

-- is_aggregate( function, description )
CREATE OR REPLACE FUNCTION is_aggregate( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, _type_func('a', $1), $2 );
$$ LANGUAGE sql;

-- is_aggregate( function )
CREATE OR REPLACE FUNCTION is_aggregate( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1,  _type_func('a', $1),
        'Function ' || quote_ident($1) || '() should be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _type_func('a', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_aggregate( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, NOT _type_func('a', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _type_func('a', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_aggregate( schema, function )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, NOT _type_func('a', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_aggregate ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _type_func('a', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_aggregate( function, args[] )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, NOT _type_func('a', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be an aggregate function'
    );
$$ LANGUAGE sql;

-- isnt_aggregate( function, description )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _type_func('a', $1), $2 );
$$ LANGUAGE sql;

-- isnt_aggregate( function )
CREATE OR REPLACE FUNCTION isnt_aggregate( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, NOT _type_func('a', $1),
        'Function ' || quote_ident($1) || '() should not be an aggregate function'
    );
$$ LANGUAGE sql;

DROP FUNCTION _agg ( NAME );
DROP FUNCTION _agg ( NAME, NAME[] );
DROP FUNCTION _agg ( NAME, NAME );
DROP FUNCTION _agg ( NAME, NAME, NAME[] );

-- is_normal_function( schema, function, args[], description )
CREATE OR REPLACE FUNCTION is_normal_function ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, _type_func('f', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- is_normal_function( schema, function, args[] )
CREATE OR REPLACE FUNCTION is_normal_function( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3,
        _type_func('f', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be a normal function'
    );
$$ LANGUAGE sql;

-- is_normal_function( schema, function, description )
CREATE OR REPLACE FUNCTION is_normal_function ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, _type_func('f', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_normal_function( schema, function )
CREATE OR REPLACE FUNCTION is_normal_function( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, _type_func('f', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should be a normal function'
    );
$$ LANGUAGE sql;

-- is_normal_function( function, args[], description )
CREATE OR REPLACE FUNCTION is_normal_function ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, _type_func('f', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_normal_function( function, args[] )
CREATE OR REPLACE FUNCTION is_normal_function( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, _type_func('f', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be a normal function'
    );
$$ LANGUAGE sql;

-- is_normal_function( function, description )
CREATE OR REPLACE FUNCTION is_normal_function( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, _type_func('f', $1), $2 );
$$ LANGUAGE sql;

-- is_normal_function( function )
CREATE OR REPLACE FUNCTION is_normal_function( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, _type_func('f', $1),
        'Function ' || quote_ident($1) || '() should be a normal function'
    );
$$ LANGUAGE sql;

-- isnt_normal_function( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_normal_function ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _type_func('f', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_normal_function( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_normal_function( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, NOT _type_func('f', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be a normal function'
    );
$$ LANGUAGE sql;

-- isnt_normal_function( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_normal_function ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _type_func('f', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_normal_function( schema, function )
CREATE OR REPLACE FUNCTION isnt_normal_function( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, NOT _type_func('f', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be a normal function'
    );
$$ LANGUAGE sql;

-- isnt_normal_function( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_normal_function ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _type_func('f', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_normal_function( function, args[] )
CREATE OR REPLACE FUNCTION isnt_normal_function( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2,
        NOT _type_func('f', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be a normal function'
    );
$$ LANGUAGE sql;

-- isnt_normal_function( function, description )
CREATE OR REPLACE FUNCTION isnt_normal_function( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _type_func('f', $1), $2 );
$$ LANGUAGE sql;

-- isnt_normal_function( function )
CREATE OR REPLACE FUNCTION isnt_normal_function( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, NOT _type_func('f', $1),
        'Function ' || quote_ident($1) || '() should not be a normal function'
    );
$$ LANGUAGE sql;

-- is_window( schema, function, args[], description )
CREATE OR REPLACE FUNCTION is_window ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, _type_func( 'w', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- is_window( schema, function, args[] )
CREATE OR REPLACE FUNCTION is_window( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, _type_func('w', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be a window function'
    );
$$ LANGUAGE sql;

-- is_window( schema, function, description )
CREATE OR REPLACE FUNCTION is_window ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, _type_func('w', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_window( schema, function )
CREATE OR REPLACE FUNCTION is_window( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, _type_func('w', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should be a window function'
    );
$$ LANGUAGE sql;

-- is_window( function, args[], description )
CREATE OR REPLACE FUNCTION is_window ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, _type_func('w', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_window( function, args[] )
CREATE OR REPLACE FUNCTION is_window( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, _type_func('w', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be a window function'
    );
$$ LANGUAGE sql;

-- is_window( function, description )
CREATE OR REPLACE FUNCTION is_window( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, _type_func('w', $1), $2 );
$$ LANGUAGE sql;

-- is_window( function )
CREATE OR REPLACE FUNCTION is_window( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, _type_func('w', $1),
        'Function ' || quote_ident($1) || '() should be a window function'
    );
$$ LANGUAGE sql;

-- isnt_window( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_window ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _type_func('w', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_window( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_window( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, NOT _type_func('w', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be a window function'
    );
$$ LANGUAGE sql;

-- isnt_window( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_window ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _type_func('w', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_window( schema, function )
CREATE OR REPLACE FUNCTION isnt_window( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, NOT _type_func('w', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be a window function'
    );
$$ LANGUAGE sql;

-- isnt_window( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_window ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _type_func('w', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_window( function, args[] )
CREATE OR REPLACE FUNCTION isnt_window( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, NOT _type_func('w', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be a window function'
    );
$$ LANGUAGE sql;

-- isnt_window( function, description )
CREATE OR REPLACE FUNCTION isnt_window( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _type_func('w', $1), $2 );
$$ LANGUAGE sql;

-- isnt_window( function )
CREATE OR REPLACE FUNCTION isnt_window( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, NOT _type_func('w', $1),
        'Function ' || quote_ident($1) || '() should not be a window function'
    );
$$ LANGUAGE sql;

-- is_procedure( schema, function, args[], description )
CREATE OR REPLACE FUNCTION is_procedure ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, _type_func( 'p', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- is_procedure( schema, function, args[] )
CREATE OR REPLACE FUNCTION is_procedure( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, _type_func('p', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be a procedure'
    );
$$ LANGUAGE sql;

-- is_procedure( schema, function, description )
CREATE OR REPLACE FUNCTION is_procedure ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, _type_func('p', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_procedure( schema, function )
CREATE OR REPLACE FUNCTION is_procedure( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, _type_func('p', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should be a procedure'
    );
$$ LANGUAGE sql;

-- is_procedure( function, args[], description )
CREATE OR REPLACE FUNCTION is_procedure ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, _type_func('p', $1, $2), $3 );
$$ LANGUAGE SQL;

-- is_procedure( function, args[] )
CREATE OR REPLACE FUNCTION is_procedure( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, _type_func('p', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be a procedure'
    );
$$ LANGUAGE sql;

-- is_procedure( function, description )
CREATE OR REPLACE FUNCTION is_procedure( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, _type_func('p', $1), $2 );
$$ LANGUAGE sql;

-- is_procedure( function )
CREATE OR REPLACE FUNCTION is_procedure( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, _type_func('p', $1),
        'Function ' || quote_ident($1) || '() should be a procedure'
    );
$$ LANGUAGE sql;

-- isnt_procedure( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_procedure ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _type_func('p', $1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_procedure( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_procedure( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2, $3, NOT _type_func('p', $1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be a procedure'
    );
$$ LANGUAGE sql;

-- isnt_procedure( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_procedure ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _type_func('p', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_procedure( schema, function )
CREATE OR REPLACE FUNCTION isnt_procedure( NAME, NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        $1, $2,  NOT _type_func('p', $1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be a procedure'
    );
$$ LANGUAGE sql;

-- isnt_procedure( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_procedure ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _type_func('p', $1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_procedure( function, args[] )
CREATE OR REPLACE FUNCTION isnt_procedure( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, $2, NOT _type_func('p', $1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be a procedure'
    );
$$ LANGUAGE sql;

-- isnt_procedure( function, description )
CREATE OR REPLACE FUNCTION isnt_procedure( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _type_func('p', $1), $2 );
$$ LANGUAGE sql;

-- isnt_procedure( function )
CREATE OR REPLACE FUNCTION isnt_procedure( NAME )
RETURNS TEXT AS $$
    SELECT _func_compare(
        NULL, $1, NOT _type_func('p', $1),
        'Function ' || quote_ident($1) || '() should not be a procedure'
    );
$$ LANGUAGE sql;
