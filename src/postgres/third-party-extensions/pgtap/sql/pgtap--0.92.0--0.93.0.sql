CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.93;'
LANGUAGE SQL IMMUTABLE;

-- _get_schema_owner( schema )
CREATE OR REPLACE FUNCTION _get_schema_owner( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(nspowner)
      FROM pg_catalog.pg_namespace
     WHERE nspname = $1;
$$ LANGUAGE SQL;

-- schema_owner_is ( schema, user, description )
CREATE OR REPLACE FUNCTION schema_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_schema_owner($1);
BEGIN
    -- Make sure the schema exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Schema ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- schema_owner_is ( schema, user )
CREATE OR REPLACE FUNCTION schema_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT schema_owner_is(
        $1, $2,
        'Schema ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _constraint ( NAME, NAME, CHAR, NAME[], TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    akey NAME[];
    keys TEXT[] := '{}';
    have TEXT;
BEGIN
    FOR akey IN SELECT * FROM _keys($1, $2, $3) LOOP
        IF akey = $4 THEN RETURN pass($5); END IF;
        keys = keys || akey::text;
    END LOOP;
    IF array_upper(keys, 0) = 1 THEN
        have := 'No ' || $6 || ' constraints';
    ELSE
        have := array_to_string(keys, E'\n              ');
    END IF;

    RETURN fail($5) || E'\n' || diag(
             '        have: ' || have
       || E'\n        want: ' || CASE WHEN $4 IS NULL THEN 'NULL' ELSE $4::text END
    );
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _constraint ( NAME, CHAR, NAME[], TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    akey NAME[];
    keys TEXT[] := '{}';
    have TEXT;
BEGIN
    FOR akey IN SELECT * FROM _keys($1, $2) LOOP
        IF akey = $3 THEN RETURN pass($4); END IF;
        keys = keys || akey::text;
    END LOOP;
    IF array_upper(keys, 0) = 1 THEN
        have := 'No ' || $5 || ' constraints';
    ELSE
        have := array_to_string(keys, E'\n              ');
    END IF;

    RETURN fail($4) || E'\n' || diag(
             '        have: ' || have
       || E'\n        want: ' || CASE WHEN $3 IS NULL THEN 'NULL' ELSE $3::text END
    );
END;
$$ LANGUAGE plpgsql;

-- fk_ok( fk_table, fk_column[], pk_table, pk_column[], description )
CREATE OR REPLACE FUNCTION fk_ok ( NAME, NAME[], NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    tab  name;
    cols name[];
BEGIN
    SELECT pk_table_name, pk_columns
      FROM pg_all_foreign_keys
     WHERE fk_table_name = $1
       AND fk_columns    = $2
       AND pg_catalog.pg_table_is_visible(fk_table_oid)
      INTO tab, cols;

    RETURN is(
        -- have
        $1 || '(' || _ident_array_to_string( $2, ', ' )
        || ') REFERENCES ' || COALESCE( tab || '(' || _ident_array_to_string( cols, ', ' ) || ')', 'NOTHING'),
        -- want
        $1 || '(' || _ident_array_to_string( $2, ', ' )
        || ') REFERENCES ' ||
        $3 || '(' || _ident_array_to_string( $4, ', ' ) || ')',
        $5
    );
END;
$$ LANGUAGE plpgsql;

-- _keys( table, constraint_type )
CREATE OR REPLACE FUNCTION _keys ( NAME, CHAR )
RETURNS SETOF NAME[] AS $$
    SELECT _pg_sv_column_array(x.conrelid,x.conkey)
      FROM pg_catalog.pg_class c
      JOIN pg_catalog.pg_constraint x  ON c.oid = x.conrelid
       AND c.relname = $1
       AND x.contype = $2
     WHERE pg_catalog.pg_table_is_visible(c.oid)
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _trig ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_trigger t
          JOIN pg_catalog.pg_class c     ON c.oid = t.tgrelid
         WHERE c.relname = $1
           AND t.tgname  = $2
           AND pg_catalog.pg_table_is_visible(c.oid)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _fkexists ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT TRUE
           FROM pg_all_foreign_keys
          WHERE quote_ident(fk_table_name)     = quote_ident($1)
            AND pg_catalog.pg_table_is_visible(fk_table_oid)
            AND fk_columns = $2
    );
$$ LANGUAGE SQL;

DROP FUNCTION display_type ( OID, INTEGER );
DROP FUNCTION display_type ( NAME, OID, INTEGER );

CREATE OR REPLACE FUNCTION _get_col_type ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT pg_catalog.format_type(a.atttypid, a.atttypmod)
      FROM pg_catalog.pg_namespace n
      JOIN pg_catalog.pg_class c     ON n.oid = c.relnamespace
      JOIN pg_catalog.pg_attribute a ON c.oid = a.attrelid
     WHERE n.nspname = $1
       AND c.relname = $2
       AND a.attname = $3
       AND attnum    > 0
       AND NOT a.attisdropped
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_col_type ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT pg_catalog.format_type(a.atttypid, a.atttypmod)
      FROM pg_catalog.pg_attribute a
      JOIN pg_catalog.pg_class c ON  a.attrelid = c.oid
     WHERE pg_catalog.pg_table_is_visible(c.oid)
       AND c.relname = $1
       AND a.attname = $2
       AND attnum    > 0
       AND NOT a.attisdropped
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_col_ns_type ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    -- Always include the namespace.
    SELECT CASE WHEN pg_catalog.pg_type_is_visible(t.oid)
                THEN quote_ident(tn.nspname) || '.'
                ELSE ''
           END || pg_catalog.format_type(a.atttypid, a.atttypmod)
      FROM pg_catalog.pg_namespace n
      JOIN pg_catalog.pg_class c      ON n.oid = c.relnamespace
      JOIN pg_catalog.pg_attribute a  ON c.oid = a.attrelid
      JOIN pg_catalog.pg_type t       ON a.atttypid = t.oid
      JOIN pg_catalog.pg_namespace tn ON t.typnamespace = tn.oid
     WHERE n.nspname = $1
       AND c.relname = $2
       AND a.attname = $3
       AND attnum    > 0
       AND NOT a.attisdropped
$$ LANGUAGE SQL;

-- _cdi( schema, table, column, default, description )
CREATE OR REPLACE FUNCTION _cdi ( NAME, NAME, NAME, anyelement, TEXT )
RETURNS TEXT AS $$
BEGIN
    IF NOT _cexists( $1, $2, $3 ) THEN
        RETURN fail( $5 ) || E'\n'
            || diag ('    Column ' || quote_ident($1) || '.' || quote_ident($2) || '.' || quote_ident($3) || ' does not exist' );
    END IF;

    IF NOT _has_def( $1, $2, $3 ) THEN
        RETURN fail( $5 ) || E'\n'
            || diag ('    Column ' || quote_ident($1) || '.' || quote_ident($2) || '.' || quote_ident($3) || ' has no default' );
    END IF;

    RETURN _def_is(
        pg_catalog.pg_get_expr(d.adbin, d.adrelid),
        pg_catalog.format_type(a.atttypid, a.atttypmod),
        $4, $5
    )
      FROM pg_catalog.pg_namespace n, pg_catalog.pg_class c, pg_catalog.pg_attribute a,
           pg_catalog.pg_attrdef d
     WHERE n.oid = c.relnamespace
       AND c.oid = a.attrelid
       AND a.atthasdef
       AND a.attrelid = d.adrelid
       AND a.attnum = d.adnum
       AND n.nspname = $1
       AND c.relname = $2
       AND a.attnum > 0
       AND NOT a.attisdropped
       AND a.attname = $3;
END;
$$ LANGUAGE plpgsql;

-- _cdi( table, column, default, description )
CREATE OR REPLACE FUNCTION _cdi ( NAME, NAME, anyelement, TEXT )
RETURNS TEXT AS $$
BEGIN
    IF NOT _cexists( $1, $2 ) THEN
        RETURN fail( $4 ) || E'\n'
            || diag ('    Column ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist' );
    END IF;

    IF NOT _has_def( $1, $2 ) THEN
        RETURN fail( $4 ) || E'\n'
            || diag ('    Column ' || quote_ident($1) || '.' || quote_ident($2) || ' has no default' );
    END IF;

    RETURN _def_is(
        pg_catalog.pg_get_expr(d.adbin, d.adrelid),
        pg_catalog.format_type(a.atttypid, a.atttypmod),
        $3, $4
    )
      FROM pg_catalog.pg_class c, pg_catalog.pg_attribute a, pg_catalog.pg_attrdef d
     WHERE c.oid = a.attrelid
       AND pg_table_is_visible(c.oid)
       AND a.atthasdef
       AND a.attrelid = d.adrelid
       AND a.attnum = d.adnum
       AND c.relname = $1
       AND a.attnum > 0
       AND NOT a.attisdropped
       AND a.attname = $2;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _cmp_types(oid, name)
RETURNS BOOLEAN AS $$
DECLARE
    dtype TEXT := pg_catalog.format_type($1, NULL);
BEGIN
    RETURN dtype = _quote_ident_like($2, dtype);
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _get_dtype( NAME, TEXT, BOOLEAN )
RETURNS TEXT AS $$
    SELECT CASE WHEN $3 AND pg_catalog.pg_type_is_visible(t.oid)
                THEN quote_ident(tn.nspname) || '.'
                ELSE ''
            END || pg_catalog.format_type(t.oid, t.typtypmod)
      FROM pg_catalog.pg_type d
      JOIN pg_catalog.pg_namespace dn ON d.typnamespace = dn.oid
      JOIN pg_catalog.pg_type t       ON d.typbasetype  = t.oid
      JOIN pg_catalog.pg_namespace tn ON t.typnamespace = tn.oid
     WHERE d.typisdefined
       AND dn.nspname = $1
       AND d.typname  = LOWER($2)
       AND d.typtype  = 'd'
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_dtype( NAME )
RETURNS TEXT AS $$
    SELECT pg_catalog.format_type(t.oid, t.typtypmod)
      FROM pg_catalog.pg_type d
      JOIN pg_catalog.pg_type t  ON d.typbasetype  = t.oid
     WHERE d.typisdefined
       AND pg_catalog.pg_type_is_visible(d.oid)
       AND d.typname = LOWER($1)
       AND d.typtype = 'd'
$$ LANGUAGE sql;

-- casts_are( casts[], description )
CREATE OR REPLACE FUNCTION casts_are ( TEXT[], TEXT )
RETURNS TEXT AS $$
    SELECT _areni(
        'casts',
        ARRAY(
            SELECT pg_catalog.format_type(castsource, NULL)
                   || ' AS ' || pg_catalog.format_type(casttarget, NULL)
              FROM pg_catalog.pg_cast c
            EXCEPT
            SELECT $1[i]
              FROM generate_series(1, array_upper($1, 1)) s(i)
        ),
        ARRAY(
            SELECT $1[i]
              FROM generate_series(1, array_upper($1, 1)) s(i)
            EXCEPT
            SELECT pg_catalog.format_type(castsource, NULL)
                   || ' AS ' || pg_catalog.format_type(casttarget, NULL)
              FROM pg_catalog.pg_cast c
        ),
        $2
    );
$$ LANGUAGE sql;

-- _get_tablespace_owner( tablespace )
CREATE OR REPLACE FUNCTION _get_tablespace_owner( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(spcowner)
      FROM pg_catalog.pg_tablespace
     WHERE spcname = $1;
$$ LANGUAGE SQL;

-- tablespace_owner_is ( tablespace, user, description )
CREATE OR REPLACE FUNCTION tablespace_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_tablespace_owner($1);
BEGIN
    -- Make sure the tablespace exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Tablespace ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- tablespace_owner_is ( tablespace, user )
CREATE OR REPLACE FUNCTION tablespace_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT tablespace_owner_is(
        $1, $2,
        'Tablespace ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _have_index( NAME, NAME, NAME)
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
    SELECT TRUE
      FROM pg_catalog.pg_index x
      JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
      JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
      JOIN pg_catalog.pg_namespace n ON n.oid = ct.relnamespace
     WHERE n.nspname  = $1
       AND ct.relname = $2
       AND ci.relname = $3
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _have_index( NAME, NAME)
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
    SELECT TRUE
      FROM pg_catalog.pg_index x
      JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
      JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
     WHERE ct.relname = $1
       AND ci.relname = $2
       AND pg_catalog.pg_table_is_visible(ct.oid)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_index_owner( NAME, NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(ci.relowner)
      FROM pg_catalog.pg_index x
      JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
      JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
      JOIN pg_catalog.pg_namespace n ON n.oid = ct.relnamespace
     WHERE n.nspname  = $1
       AND ct.relname = $2
       AND ci.relname = $3;
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_index_owner( NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(ci.relowner)
      FROM pg_catalog.pg_index x
      JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
      JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
     WHERE ct.relname = $1
       AND ci.relname = $2
       AND pg_catalog.pg_table_is_visible(ct.oid);
$$ LANGUAGE sql;

-- index_owner_is ( schema, table, index, user, description )
CREATE OR REPLACE FUNCTION index_owner_is ( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_index_owner($1, $2, $3);
BEGIN
    -- Make sure the index exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            E'    Index ' || quote_ident($3) || ' ON '
            || quote_ident($1) || '.' || quote_ident($2) || ' not found'
        );
    END IF;

    RETURN is(owner, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- index_owner_is ( schema, table, index, user )
CREATE OR REPLACE FUNCTION index_owner_is ( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT index_owner_is(
        $1, $2, $3, $4,
        'Index ' || quote_ident($3) || ' ON '
        || quote_ident($1) || '.' || quote_ident($2)
        || ' should be owned by ' || quote_ident($4)
    );
$$ LANGUAGE sql;

-- index_owner_is ( table, index, user, description )
CREATE OR REPLACE FUNCTION index_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_index_owner($1, $2);
BEGIN
    -- Make sure the index exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Index ' || quote_ident($2) || ' ON ' || quote_ident($1) || ' not found'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- index_owner_is ( table, index, user )
CREATE OR REPLACE FUNCTION index_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT index_owner_is(
        $1, $2, $3,
        'Index ' || quote_ident($2) || ' ON '
        || quote_ident($1) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- _get_language_owner( language )
CREATE OR REPLACE FUNCTION _get_language_owner( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(lanowner)
      FROM pg_catalog.pg_language
     WHERE lanname = $1;
$$ LANGUAGE SQL;

-- language_owner_is ( language, user, description )
CREATE OR REPLACE FUNCTION language_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_language_owner($1);
BEGIN
    -- Make sure the language exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Language ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- language_owner_is ( language, user )
CREATE OR REPLACE FUNCTION language_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT language_owner_is(
        $1, $2,
        'Language ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_opclass_owner ( NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(opcowner)
      FROM pg_catalog.pg_opclass oc
      JOIN pg_catalog.pg_namespace n ON oc.opcnamespace = n.oid
     WHERE n.nspname = $1
       AND opcname   = $2;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_opclass_owner ( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(opcowner)
      FROM pg_catalog.pg_opclass
     WHERE opcname = $1
       AND pg_catalog.pg_opclass_is_visible(oid);
$$ LANGUAGE SQL;

-- opclass_owner_is( schema, opclass, user, description )
CREATE OR REPLACE FUNCTION opclass_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_opclass_owner($1, $2);
BEGIN
    -- Make sure the opclass exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Operator class ' || quote_ident($1) || '.' || quote_ident($2)
            || ' not found'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- opclass_owner_is( schema, opclass, user )
CREATE OR REPLACE FUNCTION opclass_owner_is( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT opclass_owner_is(
        $1, $2, $3,
        'Operator class ' || quote_ident($1) || '.' || quote_ident($2) ||
        ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- opclass_owner_is( opclass, user, description )
CREATE OR REPLACE FUNCTION opclass_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_opclass_owner($1);
BEGIN
    -- Make sure the opclass exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Operator class ' || quote_ident($1) || ' not found'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- opclass_owner_is( opclass, user )
CREATE OR REPLACE FUNCTION opclass_owner_is( NAME, NAME )
RETURNS TEXT AS $$
    SELECT opclass_owner_is(
        $1, $2,
        'Operator class ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _opc_exists( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
        SELECT TRUE
          FROM pg_catalog.pg_opclass oc
          JOIN pg_catalog.pg_namespace n ON oc.opcnamespace = n.oid
         WHERE n.nspname  = $1
           AND oc.opcname = $2
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _opc_exists( NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS (
        SELECT TRUE
          FROM pg_catalog.pg_opclass oc
         WHERE oc.opcname = $1
           AND pg_opclass_is_visible(oid)
    );
$$ LANGUAGE SQL;

-- has_opclass( name, description )
CREATE OR REPLACE FUNCTION has_opclass( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _opc_exists( $1 ), $2)
$$ LANGUAGE SQL;

-- has_opclass( name )
CREATE OR REPLACE FUNCTION has_opclass( NAME )
RETURNS TEXT AS $$
    SELECT ok( _opc_exists( $1 ), 'Operator class ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_opclass( name, description )
CREATE OR REPLACE FUNCTION hasnt_opclass( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _opc_exists( $1 ), $2)
$$ LANGUAGE SQL;

-- hasnt_opclass( name )
CREATE OR REPLACE FUNCTION hasnt_opclass( NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _opc_exists( $1 ), 'Operator class ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_type_owner ( NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(t.typowner)
      FROM pg_catalog.pg_type t
      JOIN pg_catalog.pg_namespace n ON n.oid = t.typnamespace
     WHERE n.nspname = $1
       AND t.typname = $2
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_type_owner ( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(typowner)
      FROM pg_catalog.pg_type
     WHERE typname = $1
       AND pg_catalog.pg_type_is_visible(oid)
$$ LANGUAGE SQL;

-- type_owner_is ( schema, type, user, description )
CREATE OR REPLACE FUNCTION type_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_type_owner($1, $2);
BEGIN
    -- Make sure the type exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Type ' || quote_ident($1) || '.' || quote_ident($2) || ' not found'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- type_owner_is ( schema, type, user )
CREATE OR REPLACE FUNCTION type_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT type_owner_is(
        $1, $2, $3,
        'Type ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- type_owner_is ( type, user, description )
CREATE OR REPLACE FUNCTION type_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_type_owner($1);
BEGIN
    -- Make sure the type exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Type ' || quote_ident($1) || ' not found'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- type_owner_is ( type, user )
CREATE OR REPLACE FUNCTION type_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT type_owner_is(
        $1, $2,
        'Type ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;
