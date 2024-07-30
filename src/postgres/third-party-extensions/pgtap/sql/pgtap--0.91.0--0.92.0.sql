CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.92;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION _cexists ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_class c
          JOIN pg_catalog.pg_attribute a ON c.oid = a.attrelid
         WHERE c.relname = $1
           AND pg_catalog.pg_table_is_visible(c.oid)
           AND a.attnum > 0
           AND NOT a.attisdropped
           AND a.attname = $2
    );
$$ LANGUAGE SQL;

-- has_foreign_table( schema, table, description )
CREATE OR REPLACE FUNCTION has_foreign_table ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'f', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_foreign_table( table, description )
CREATE OR REPLACE FUNCTION has_foreign_table ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'f', $1 ), $2 );
$$ LANGUAGE SQL;

-- has_foreign_table( table )
CREATE OR REPLACE FUNCTION has_foreign_table ( NAME )
RETURNS TEXT AS $$
    SELECT has_foreign_table( $1, 'Foreign table ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_foreign_table( schema, table, description )
CREATE OR REPLACE FUNCTION hasnt_foreign_table ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'f', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_foreign_table( table, description )
CREATE OR REPLACE FUNCTION hasnt_foreign_table ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'f', $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_foreign_table( table )
CREATE OR REPLACE FUNCTION hasnt_foreign_table ( NAME )
RETURNS TEXT AS $$
    SELECT hasnt_foreign_table( $1, 'Foreign table ' || quote_ident($1) || ' should not exist' );
$$ LANGUAGE SQL;


-- has_composite( schema, type, description )
CREATE OR REPLACE FUNCTION has_composite ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'c', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_composite( type, description )
CREATE OR REPLACE FUNCTION has_composite ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'c', $1 ), $2 );
$$ LANGUAGE SQL;

-- has_composite( type )
CREATE OR REPLACE FUNCTION has_composite ( NAME )
RETURNS TEXT AS $$
    SELECT has_composite( $1, 'Composite type ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_composite( schema, type, description )
CREATE OR REPLACE FUNCTION hasnt_composite ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'c', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_composite( type, description )
CREATE OR REPLACE FUNCTION hasnt_composite ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'c', $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_composite( type )
CREATE OR REPLACE FUNCTION hasnt_composite ( NAME )
RETURNS TEXT AS $$
    SELECT hasnt_composite( $1, 'Composite type ' || quote_ident($1) || ' should not exist' );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
     WHERE n.nspname = $1
       AND c.relname = $2
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
     WHERE c.relname = $1
       AND pg_catalog.pg_table_is_visible(c.oid)
$$ LANGUAGE SQL;

-- relation_owner_is ( schema, relation, user, description )
CREATE OR REPLACE FUNCTION relation_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner($1, $2);
BEGIN
    -- Make sure the relation exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Relation ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- relation_owner_is ( schema, relation, user )
CREATE OR REPLACE FUNCTION relation_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT relation_owner_is(
        $1, $2, $3,
        'Relation ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- relation_owner_is ( relation, user, description )
CREATE OR REPLACE FUNCTION relation_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner($1);
BEGIN
    -- Make sure the relation exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Relation ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- relation_owner_is ( relation, user )
CREATE OR REPLACE FUNCTION relation_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT relation_owner_is(
        $1, $2,
        'Relation ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR, NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
     WHERE c.relkind = $1
       AND n.nspname = $2
       AND c.relname = $3
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
     WHERE c.relkind = $1
       AND c.relname = $2
       AND pg_catalog.pg_table_is_visible(c.oid)
$$ LANGUAGE SQL;

-- table_owner_is ( schema, table, user, description )
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('r'::char, $1, $2);
BEGIN
    -- Make sure the table exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- table_owner_is ( schema, table, user )
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT table_owner_is(
        $1, $2, $3,
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- table_owner_is ( table, user, description )
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('r'::char, $1);
BEGIN
    -- Make sure the table exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Table ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- table_owner_is ( table, user )
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT table_owner_is(
        $1, $2,
        'Table ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- view_owner_is ( schema, view, user, description )
CREATE OR REPLACE FUNCTION view_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('v'::char, $1, $2);
BEGIN
    -- Make sure the view exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    View ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- view_owner_is ( schema, view, user )
CREATE OR REPLACE FUNCTION view_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT view_owner_is(
        $1, $2, $3,
        'View ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- view_owner_is ( view, user, description )
CREATE OR REPLACE FUNCTION view_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('v'::char, $1);
BEGIN
    -- Make sure the view exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    View ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- view_owner_is ( view, user )
CREATE OR REPLACE FUNCTION view_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT view_owner_is(
        $1, $2,
        'View ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- sequence_owner_is ( schema, sequence, user, description )
CREATE OR REPLACE FUNCTION sequence_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('S'::char, $1, $2);
BEGIN
    -- Make sure the sequence exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Sequence ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- sequence_owner_is ( schema, sequence, user )
CREATE OR REPLACE FUNCTION sequence_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT sequence_owner_is(
        $1, $2, $3,
        'Sequence ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- sequence_owner_is ( sequence, user, description )
CREATE OR REPLACE FUNCTION sequence_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('S'::char, $1);
BEGIN
    -- Make sure the sequence exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Sequence ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- sequence_owner_is ( sequence, user )
CREATE OR REPLACE FUNCTION sequence_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT sequence_owner_is(
        $1, $2,
        'Sequence ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- composite_owner_is ( schema, composite, user, description )
CREATE OR REPLACE FUNCTION composite_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('c'::char, $1, $2);
BEGIN
    -- Make sure the composite exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Composite type ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- composite_owner_is ( schema, composite, user )
CREATE OR REPLACE FUNCTION composite_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT composite_owner_is(
        $1, $2, $3,
        'Composite type ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- composite_owner_is ( composite, user, description )
CREATE OR REPLACE FUNCTION composite_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('c'::char, $1);
BEGIN
    -- Make sure the composite exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Composite type ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- composite_owner_is ( composite, user )
CREATE OR REPLACE FUNCTION composite_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT composite_owner_is(
        $1, $2,
        'Composite type ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- foreign_table_owner_is ( schema, table, user, description )
CREATE OR REPLACE FUNCTION foreign_table_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('f'::char, $1, $2);
BEGIN
    -- Make sure the table exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Foreign table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- foreign_table_owner_is ( schema, table, user )
CREATE OR REPLACE FUNCTION foreign_table_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT foreign_table_owner_is(
        $1, $2, $3,
        'Foreign table ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- foreign_table_owner_is ( table, user, description )
CREATE OR REPLACE FUNCTION foreign_table_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('f'::char, $1);
BEGIN
    -- Make sure the table exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Foreign table ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- foreign_table_owner_is ( table, user )
CREATE OR REPLACE FUNCTION foreign_table_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT foreign_table_owner_is(
        $1, $2,
        'Foreign table ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _relexists ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE n.nspname = $1
           AND c.relname = $2
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _relexists ( NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_class c
         WHERE pg_catalog.pg_table_is_visible(c.oid)
           AND c.relname = $1
    );
$$ LANGUAGE SQL;

-- has_relation( schema, relation, description )
CREATE OR REPLACE FUNCTION has_relation ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _relexists( $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_relation( relation, description )
CREATE OR REPLACE FUNCTION has_relation ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _relexists( $1 ), $2 );
$$ LANGUAGE SQL;

-- has_relation( relation )
CREATE OR REPLACE FUNCTION has_relation ( NAME )
RETURNS TEXT AS $$
    SELECT has_relation( $1, 'Relation ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_relation( schema, relation, description )
CREATE OR REPLACE FUNCTION hasnt_relation ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _relexists( $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_relation( relation, description )
CREATE OR REPLACE FUNCTION hasnt_relation ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _relexists( $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_relation( relation )
CREATE OR REPLACE FUNCTION hasnt_relation ( NAME )
RETURNS TEXT AS $$
    SELECT hasnt_relation( $1, 'Relation ' || quote_ident($1) || ' should not exist' );
$$ LANGUAGE SQL;

DROP VIEW tap_funky;
CREATE VIEW tap_funky
 AS SELECT p.oid         AS oid,
           n.nspname     AS schema,
           p.proname     AS name,
           pg_catalog.pg_get_userbyid(p.proowner) AS owner,
           array_to_string(p.proargtypes::regtype[], ',') AS args,
           CASE p.proretset WHEN TRUE THEN 'setof ' ELSE '' END
             || p.prorettype::regtype AS returns,
           p.prolang     AS langoid,
           p.proisstrict AS is_strict,
           p.proisagg    AS is_agg,
           p.prosecdef   AS is_definer,
           p.proretset   AS returns_set,
           p.provolatile::char AS volatility,
           pg_catalog.pg_function_is_visible(p.oid) AS is_visible
      FROM pg_catalog.pg_proc p
      JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid
;

CREATE OR REPLACE FUNCTION _get_func_owner ( NAME, NAME, NAME[] )
RETURNS NAME AS $$
    SELECT owner
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = array_to_string($3, ',')
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_func_owner ( NAME, NAME[] )
RETURNS NAME AS $$
    SELECT owner
      FROM tap_funky
     WHERE name = $1
       AND args = array_to_string($2, ',')
       AND is_visible
$$ LANGUAGE SQL;

-- function_owner_is( schema, function, args[], user, description )
CREATE OR REPLACE FUNCTION function_owner_is ( NAME, NAME, NAME[], NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_func_owner($1, $2, $3);
BEGIN
    -- Make sure the function exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            E'    Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
                    array_to_string($3, ', ') || ') does not exist'
        );
    END IF;

    RETURN is(owner, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- function_owner_is( schema, function, args[], user )
CREATE OR REPLACE FUNCTION function_owner_is( NAME, NAME, NAME[], NAME )
RETURNS TEXT AS $$
    SELECT function_owner_is(
        $1, $2, $3, $4,
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should be owned by ' || quote_ident($4)
    );
$$ LANGUAGE sql;

-- function_owner_is( function, args[], user, description )
CREATE OR REPLACE FUNCTION function_owner_is ( NAME, NAME[], NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_func_owner($1, $2);
BEGIN
    -- Make sure the function exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Function ' || quote_ident($1) || '(' ||
                    array_to_string($2, ', ') || ') does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- function_owner_is( function, args[], user )
CREATE OR REPLACE FUNCTION function_owner_is( NAME, NAME[], NAME )
RETURNS TEXT AS $$
    SELECT function_owner_is(
        $1, $2, $3,
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _get_latest ( text )
RETURNS integer[] AS $$
DECLARE
    ret integer[];
BEGIN
    EXECUTE 'SELECT ARRAY[ id, value] FROM __tcache__ WHERE label = ' ||
    quote_literal($1) || ' AND id = (SELECT MAX(id) FROM __tcache__ WHERE label = ' ||
    quote_literal($1) || ') LIMIT 1' INTO ret;
    RETURN ret;
EXCEPTION WHEN undefined_table THEN
   RAISE EXCEPTION 'You tried to run a test without a plan! Gotta have a plan';
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION _trig ( NAME, NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_trigger t
          JOIN pg_catalog.pg_class c     ON c.oid = t.tgrelid
          JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
         WHERE n.nspname = $1
           AND c.relname = $2
           AND t.tgname  = $3
           AND NOT t.tgisinternal
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _trig ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_trigger t
          JOIN pg_catalog.pg_class c     ON c.oid = t.tgrelid
         WHERE c.relname = $1
           AND t.tgname  = $2
           AND NOT t.tgisinternal
    );
$$ LANGUAGE SQL;

-- triggers_are( schema, table, triggers[], description )
CREATE OR REPLACE FUNCTION triggers_are( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'triggers',
        ARRAY(
            SELECT t.tgname
              FROM pg_catalog.pg_trigger t
              JOIN pg_catalog.pg_class c     ON c.oid = t.tgrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE n.nspname = $1
               AND c.relname = $2
               AND NOT t.tgisinternal
            EXCEPT
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
        ),
        ARRAY(
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
            EXCEPT
            SELECT t.tgname
              FROM pg_catalog.pg_trigger t
              JOIN pg_catalog.pg_class c     ON c.oid = t.tgrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE n.nspname = $1
               AND c.relname = $2
               AND NOT t.tgisinternal
        ),
        $4
    );
$$ LANGUAGE SQL;

-- triggers_are( table, triggers[], description )
CREATE OR REPLACE FUNCTION triggers_are( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'triggers',
        ARRAY(
            SELECT t.tgname
              FROM pg_catalog.pg_trigger t
              JOIN pg_catalog.pg_class c ON c.oid = t.tgrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
             WHERE c.relname = $1
               AND n.nspname NOT IN ('pg_catalog', 'information_schema')
               AND NOT t.tgisinternal
            EXCEPT
            SELECT $2[i]
              FROM generate_series(1, array_upper($2, 1)) s(i)
        ),
        ARRAY(
            SELECT $2[i]
              FROM generate_series(1, array_upper($2, 1)) s(i)
            EXCEPT
            SELECT t.tgname
              FROM pg_catalog.pg_trigger t
              JOIN pg_catalog.pg_class c ON c.oid = t.tgrelid
              JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
               AND n.nspname NOT IN ('pg_catalog', 'information_schema')
               AND NOT t.tgisinternal
        ),
        $3
    );
$$ LANGUAGE SQL;

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
        RETURN ok( false, $3 ) || E'\n' || diag(
            E'    Number of columns or their types differ between the queries' ||
            CASE WHEN have_rec::TEXT = want_rec::text THEN '' ELSE E':\n' ||
                '        have: ' || CASE WHEN have_found THEN have_rec::text ELSE 'NULL' END || E'\n' ||
                '        want: ' || CASE WHEN want_found THEN want_rec::text ELSE 'NULL' END
            END
        );
END;
$$ LANGUAGE plpgsql;

-- isnt_empty( sql, description )
CREATE OR REPLACE FUNCTION isnt_empty( TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    res  BOOLEAN := FALSE;
    rec  RECORD;
BEGIN
    -- Find extra records.
    FOR rec in EXECUTE _query($1) LOOP
        res := TRUE;
        EXIT;
    END LOOP;

    RETURN ok(res, $2);
END;
$$ LANGUAGE plpgsql;

-- isnt_empty( sql )
CREATE OR REPLACE FUNCTION isnt_empty( TEXT )
RETURNS TEXT AS $$
    SELECT isnt_empty( $1, NULL );
$$ LANGUAGE sql;

DROP FUNCTION _ikeys( NAME, NAME, NAME );
DROP FUNCTION _ikeys( NAME, NAME );
DROP FUNCTION _iexpr( NAME, NAME, NAME );
DROP FUNCTION _iexpr( NAME, NAME );

CREATE OR REPLACE FUNCTION _ikeys( NAME, NAME, NAME)
RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT pg_catalog.pg_get_indexdef( ci.oid, s.i + 1, false)
          FROM pg_catalog.pg_index x
          JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
          JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
          JOIN pg_catalog.pg_namespace n ON n.oid = ct.relnamespace
          JOIN generate_series(0, current_setting('max_index_keys')::int - 1) s(i)
            ON x.indkey[s.i] IS NOT NULL
         WHERE ct.relname = $2
           AND ci.relname = $3
           AND n.nspname  = $1
         ORDER BY s.i
    );
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _ikeys( NAME, NAME)
RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT pg_catalog.pg_get_indexdef( ci.oid, s.i + 1, false)
          FROM pg_catalog.pg_index x
          JOIN pg_catalog.pg_class ct    ON ct.oid = x.indrelid
          JOIN pg_catalog.pg_class ci    ON ci.oid = x.indexrelid
          JOIN generate_series(0, current_setting('max_index_keys')::int - 1) s(i)
            ON x.indkey[s.i] IS NOT NULL
         WHERE ct.relname = $1
           AND ci.relname = $2
           AND pg_catalog.pg_table_is_visible(ct.oid)
         ORDER BY s.i
    );
$$ LANGUAGE sql;

-- has_index( schema, table, index, columns[], description )
CREATE OR REPLACE FUNCTION has_index ( NAME, NAME, NAME, NAME[], text )
RETURNS TEXT AS $$
DECLARE
     index_cols name[];
BEGIN
    index_cols := _ikeys($1, $2, $3 );

    IF index_cols IS NULL OR index_cols = '{}'::name[] THEN
        RETURN ok( false, $5 ) || E'\n'
            || diag( 'Index ' || quote_ident($3) || ' ON ' || quote_ident($1) || '.' || quote_ident($2) || ' not found');
    END IF;

    RETURN is(
        quote_ident($3) || ' ON ' || quote_ident($1) || '.' || quote_ident($2) || '(' || array_to_string( index_cols, ', ' ) || ')',
        quote_ident($3) || ' ON ' || quote_ident($1) || '.' || quote_ident($2) || '(' || array_to_string( $4, ', ' ) || ')',
        $5
    );
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION has_index ( NAME, NAME, NAME, NAME, text )
RETURNS TEXT AS $$
    SELECT has_index( $1, $2, $3, ARRAY[$4], $5 );
$$ LANGUAGE sql;

-- has_index( table, index, columns[], description )
CREATE OR REPLACE FUNCTION has_index ( NAME, NAME, NAME[], text )
RETURNS TEXT AS $$
DECLARE
     index_cols name[];
BEGIN
    index_cols := _ikeys($1, $2 );

    IF index_cols IS NULL OR index_cols = '{}'::name[] THEN
        RETURN ok( false, $4 ) || E'\n'
            || diag( 'Index ' || quote_ident($2) || ' ON ' || quote_ident($1) || ' not found');
    END IF;

    RETURN is(
        quote_ident($2) || ' ON ' || quote_ident($1) || '(' || array_to_string( index_cols, ', ' ) || ')',
        quote_ident($2) || ' ON ' || quote_ident($1) || '(' || array_to_string( $3, ', ' ) || ')',
        $4
    );
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION has_index ( NAME, NAME, NAME, text )
RETURNS TEXT AS $$
    SELECT CASE WHEN _is_schema( $1 ) THEN
        -- Looking for schema.table index.
            ok ( _have_index( $1, $2, $3 ), $4)
        ELSE
        -- Looking for particular columns.
            has_index( $1, $2, ARRAY[$3], $4 )
      END;
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION _assets_are ( text, text[], text[], TEXT )
RETURNS TEXT AS $$
    SELECT _areni(
        $1,
        ARRAY(
            SELECT UPPER($2[i]) AS thing
              FROM generate_series(1, array_upper($2, 1)) s(i)
            EXCEPT
            SELECT $3[i]
              FROM generate_series(1, array_upper($3, 1)) s(i)
             ORDER BY thing
        ),
        ARRAY(
            SELECT $3[i] AS thing
              FROM generate_series(1, array_upper($3, 1)) s(i)
            EXCEPT
            SELECT UPPER($2[i])
              FROM generate_series(1, array_upper($2, 1)) s(i)
             ORDER BY thing
        ),
        $4
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_table_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := _table_privs();
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        BEGIN
            IF pg_catalog.has_table_privilege($1, $2, privs[i]) THEN
                grants := grants || privs[i];
            END IF;
        EXCEPTION WHEN undefined_table THEN
            -- Not a valid table name.
            RETURN '{undefined_table}';
        WHEN undefined_object THEN
            -- Not a valid role.
            RETURN '{undefined_role}';
        WHEN invalid_parameter_value THEN
            -- Not a valid permission on this version of PostgreSQL; ignore;
        END;
    END LOOP;
    RETURN grants;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _table_privs()
RETURNS NAME[] AS $$
DECLARE
    pgversion INTEGER := pg_version_num();
BEGIN
    IF pgversion < 80200 THEN RETURN ARRAY[
        'DELETE', 'INSERT', 'REFERENCES', 'RULE', 'SELECT', 'TRIGGER', 'UPDATE'
    ];
    ELSIF pgversion < 80400 THEN RETURN ARRAY[
        'DELETE', 'INSERT', 'REFERENCES', 'SELECT', 'TRIGGER', 'UPDATE'
    ];
    ELSE RETURN ARRAY[
        'DELETE', 'INSERT', 'REFERENCES', 'SELECT', 'TRIGGER', 'TRUNCATE', 'UPDATE'
    ];
    END IF;
END;
$$ language plpgsql;

-- table_privs_are ( schema, table, user, privileges[], description )
CREATE OR REPLACE FUNCTION table_privs_are ( NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_table_privs( $3, quote_ident($1) || '.' || quote_ident($2) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Role ' || quote_ident($3) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- table_privs_are ( schema, table, user, privileges[] )
CREATE OR REPLACE FUNCTION table_privs_are ( NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT table_privs_are(
        $1, $2, $3, $4,
        'Role ' || quote_ident($3) || ' should be granted '
            || CASE WHEN $4[1] IS NULL THEN 'no privileges' ELSE array_to_string($4, ', ') END
            || ' on table ' || quote_ident($1) || '.' || quote_ident($2)
    );
$$ LANGUAGE SQL;

-- table_privs_are ( table, user, privileges[], description )
CREATE OR REPLACE FUNCTION table_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_table_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- table_privs_are ( table, user, privileges[] )
CREATE OR REPLACE FUNCTION table_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT table_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on table ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _db_privs()
RETURNS NAME[] AS $$
DECLARE
    pgversion INTEGER := pg_version_num();
BEGIN
    IF pgversion < 80200 THEN
        RETURN ARRAY['CREATE', 'TEMPORARY'];
    ELSE
        RETURN ARRAY['CREATE', 'CONNECT', 'TEMPORARY'];
    END IF;
END;
$$ language plpgsql;

CREATE OR REPLACE FUNCTION _get_db_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := _db_privs();
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        BEGIN
            IF pg_catalog.has_database_privilege($1, $2, privs[i]) THEN
                grants := grants || privs[i];
            END IF;
        EXCEPTION WHEN invalid_catalog_name THEN
            -- Not a valid db name.
            RETURN '{invalid_catalog_name}';
        WHEN undefined_object THEN
            -- Not a valid role.
            RETURN '{undefined_role}';
        WHEN invalid_parameter_value THEN
            -- Not a valid permission on this version of PostgreSQL; ignore;
        END;
    END LOOP;
    RETURN grants;
END;
$$ LANGUAGE plpgsql;

-- database_privs_are ( db, user, privileges[], description )
CREATE OR REPLACE FUNCTION database_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_db_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'invalid_catalog_name' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Database ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- database_privs_are ( db, user, privileges[] )
CREATE OR REPLACE FUNCTION database_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT database_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on database ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_func_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_function_privilege($1, $2, 'EXECUTE') THEN
        RETURN '{EXECUTE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION
    -- Not a valid func name.
    WHEN undefined_function THEN RETURN '{undefined_function}';
    -- Not a valid role.
    WHEN undefined_object   THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _fprivs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_func_privs($2, $1);
BEGIN
    IF grants[1] = 'undefined_function' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Function ' || $1 || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- function_privs_are ( schema, function, args[], user, privileges[], description )
CREATE OR REPLACE FUNCTION function_privs_are ( NAME, NAME, NAME[], NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _fprivs_are(
        quote_ident($1) || '.' || quote_ident($2) || '(' || array_to_string($3, ', ') || ')',
        $4, $5, $6
    );
$$ LANGUAGE SQL;

-- function_privs_are ( schema, function, args[], user, privileges[] )
CREATE OR REPLACE FUNCTION function_privs_are ( NAME, NAME, NAME[], NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT function_privs_are(
        $1, $2, $3, $4, $5,
        'Role ' || quote_ident($4) || ' should be granted '
            || CASE WHEN $5[1] IS NULL THEN 'no privileges' ELSE array_to_string($5, ', ') END
            || ' on function ' || quote_ident($1) || '.' || quote_ident($2)
            || '(' || array_to_string($3, ', ') || ')'
    );
$$ LANGUAGE SQL;

-- function_privs_are ( function, args[], user, privileges[], description )
CREATE OR REPLACE FUNCTION function_privs_are ( NAME, NAME[], NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _fprivs_are(
        quote_ident($1) || '(' || array_to_string($2, ', ') || ')',
        $3, $4, $5
    );
$$ LANGUAGE SQL;

-- function_privs_are ( function, args[], user, privileges[] )
CREATE OR REPLACE FUNCTION function_privs_are ( NAME, NAME[], NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT function_privs_are(
        $1, $2, $3, $4,
        'Role ' || quote_ident($3) || ' should be granted '
            || CASE WHEN $4[1] IS NULL THEN 'no privileges' ELSE array_to_string($4, ', ') END
            || ' on function ' || quote_ident($1) || '(' || array_to_string($2, ', ') || ')'
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_lang_privs (NAME, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_language_privilege($1, $2, 'USAGE') THEN
        RETURN '{USAGE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION WHEN undefined_object THEN
    -- Same error code for unknown user or language. So figure out which.
    RETURN CASE WHEN SQLERRM LIKE '%' || $1 || '%' THEN
        '{undefined_role}'
    ELSE
        '{undefined_language}'
    END;
END;
$$ LANGUAGE plpgsql;

-- language_privs_are ( lang, user, privileges[], description )
CREATE OR REPLACE FUNCTION language_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_lang_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_language' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Language ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- language_privs_are ( lang, user, privileges[] )
CREATE OR REPLACE FUNCTION language_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT language_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on language ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_schema_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := ARRAY['CREATE', 'USAGE'];
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        IF pg_catalog.has_schema_privilege($1, $2, privs[i]) THEN
            grants := grants || privs[i];
        END IF;
    END LOOP;
    RETURN grants;
EXCEPTION
    -- Not a valid schema name.
    WHEN invalid_schema_name THEN RETURN '{invalid_schema_name}';
    -- Not a valid role.
    WHEN undefined_object   THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

-- schema_privs_are ( schema, user, privileges[], description )
CREATE OR REPLACE FUNCTION schema_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_schema_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'invalid_schema_name' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Schema ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- schema_privs_are ( schema, user, privileges[] )
CREATE OR REPLACE FUNCTION schema_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT schema_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on schema ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_tablespaceprivs (NAME, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_tablespace_privilege($1, $2, 'CREATE') THEN
        RETURN '{CREATE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION WHEN undefined_object THEN
    -- Same error code for unknown user or tablespace. So figure out which.
    RETURN CASE WHEN SQLERRM LIKE '%' || $1 || '%' THEN
        '{undefined_role}'
    ELSE
        '{undefined_tablespace}'
    END;
END;
$$ LANGUAGE plpgsql;

-- tablespace_privs_are ( tablespace, user, privileges[], description )
CREATE OR REPLACE FUNCTION tablespace_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_tablespaceprivs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_tablespace' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Tablespace ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- tablespace_privs_are ( tablespace, user, privileges[] )
CREATE OR REPLACE FUNCTION tablespace_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT tablespace_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on tablespace ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_sequence_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := ARRAY['SELECT', 'UPDATE', 'USAGE'];
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        BEGIN
            IF pg_catalog.has_sequence_privilege($1, $2, privs[i]) THEN
                grants := grants || privs[i];
            END IF;
        EXCEPTION WHEN undefined_table THEN
            -- Not a valid sequence name.
            RETURN '{undefined_table}';
        WHEN undefined_object THEN
            -- Not a valid role.
            RETURN '{undefined_role}';
        WHEN invalid_parameter_value THEN
            -- Not a valid permission on this version of PostgreSQL; ignore;
        END;
    END LOOP;
    RETURN grants;
END;
$$ LANGUAGE plpgsql;

-- sequence_privs_are ( schema, sequence, user, privileges[], description )
CREATE OR REPLACE FUNCTION sequence_privs_are ( NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_sequence_privs( $3, quote_ident($1) || '.' || quote_ident($2) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Sequence ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Role ' || quote_ident($3) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- sequence_privs_are ( schema, sequence, user, privileges[] )
CREATE OR REPLACE FUNCTION sequence_privs_are ( NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT sequence_privs_are(
        $1, $2, $3, $4,
        'Role ' || quote_ident($3) || ' should be granted '
            || CASE WHEN $4[1] IS NULL THEN 'no privileges' ELSE array_to_string($4, ', ') END
            || ' on sequence '|| quote_ident($1) || '.' || quote_ident($2)
    );
$$ LANGUAGE SQL;

-- sequence_privs_are ( sequence, user, privileges[], description )
CREATE OR REPLACE FUNCTION sequence_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_sequence_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Sequence ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- sequence_privs_are ( sequence, user, privileges[] )
CREATE OR REPLACE FUNCTION sequence_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT sequence_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on sequence ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_ac_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'];
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        BEGIN
            IF pg_catalog.has_any_column_privilege($1, $2, privs[i]) THEN
                grants := grants || privs[i];
            END IF;
        EXCEPTION WHEN undefined_table THEN
            -- Not a valid table name.
            RETURN '{undefined_table}';
        WHEN undefined_object THEN
            -- Not a valid role.
            RETURN '{undefined_role}';
        WHEN invalid_parameter_value THEN
            -- Not a valid permission on this version of PostgreSQL; ignore;
        END;
    END LOOP;
    RETURN grants;
END;
$$ LANGUAGE plpgsql;

-- any_column_privs_are ( schema, table, user, privileges[], description )
CREATE OR REPLACE FUNCTION any_column_privs_are ( NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_ac_privs( $3, quote_ident($1) || '.' || quote_ident($2) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Role ' || quote_ident($3) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- any_column_privs_are ( schema, table, user, privileges[] )
CREATE OR REPLACE FUNCTION any_column_privs_are ( NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT any_column_privs_are(
        $1, $2, $3, $4,
        'Role ' || quote_ident($3) || ' should be granted '
            || CASE WHEN $4[1] IS NULL THEN 'no privileges' ELSE array_to_string($4, ', ') END
            || ' on any column in '|| quote_ident($1) || '.' || quote_ident($2)
    );
$$ LANGUAGE SQL;

-- any_column_privs_are ( table, user, privileges[], description )
CREATE OR REPLACE FUNCTION any_column_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_ac_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- any_column_privs_are ( table, user, privileges[] )
CREATE OR REPLACE FUNCTION any_column_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT any_column_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on any column in ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_col_privs(NAME, TEXT, NAME)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := ARRAY['INSERT', 'REFERENCES', 'SELECT', 'UPDATE'];
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        IF pg_catalog.has_column_privilege($1, $2, $3, privs[i]) THEN
            grants := grants || privs[i];
        END IF;
    END LOOP;
    RETURN grants;
EXCEPTION
    -- Not a valid column name.
    WHEN undefined_column THEN RETURN '{undefined_column}';
    -- Not a valid table name.
    WHEN undefined_table THEN RETURN '{undefined_table}';
    -- Not a valid role.
    WHEN undefined_object THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

-- column_privs_are ( schema, table, column, user, privileges[], description )
CREATE OR REPLACE FUNCTION column_privs_are ( NAME, NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_col_privs( $4, quote_ident($1) || '.' || quote_ident($2), $3 );
BEGIN
    IF grants[1] = 'undefined_column' THEN
        RETURN ok(FALSE, $6) || E'\n' || diag(
            '    Column ' || quote_ident($1) || '.' || quote_ident($2) || '.' || quote_ident($3)
            || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $6) || E'\n' || diag(
            '    Table ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $6) || E'\n' || diag(
            '    Role ' || quote_ident($4) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $5, $6);
END;
$$ LANGUAGE plpgsql;

-- column_privs_are ( schema, table, column, user, privileges[] )
CREATE OR REPLACE FUNCTION column_privs_are ( NAME, NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT column_privs_are(
        $1, $2, $3, $4, $5,
        'Role ' || quote_ident($4) || ' should be granted '
            || CASE WHEN $5[1] IS NULL THEN 'no privileges' ELSE array_to_string($5, ', ') END
            || ' on column ' || quote_ident($1) || '.' || quote_ident($2) || '.' || quote_ident($3)
    );
$$ LANGUAGE SQL;

-- column_privs_are ( table, column, user, privileges[], description )
CREATE OR REPLACE FUNCTION column_privs_are ( NAME, NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_col_privs( $3, quote_ident($1), $2 );
BEGIN
    IF grants[1] = 'undefined_column' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Column ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_table' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Table ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $5) || E'\n' || diag(
            '    Role ' || quote_ident($3) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $4, $5);
END;
$$ LANGUAGE plpgsql;

-- column_privs_are ( table, column, user, privileges[] )
CREATE OR REPLACE FUNCTION column_privs_are ( NAME, NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT column_privs_are(
        $1, $2, $3, $4,
        'Role ' || quote_ident($3) || ' should be granted '
            || CASE WHEN $4[1] IS NULL THEN 'no privileges' ELSE array_to_string($4, ', ') END
            || ' on column ' || quote_ident($1) || '.' || quote_ident($2)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_fdw_privs (NAME, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_foreign_data_wrapper_privilege($1, $2, 'USAGE') THEN
        RETURN '{USAGE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION WHEN undefined_object THEN
    -- Same error code for unknown user or fdw. So figure out which.
    RETURN CASE WHEN SQLERRM LIKE '%' || $1 || '%' THEN
        '{undefined_role}'
    ELSE
        '{undefined_fdw}'
    END;
END;
$$ LANGUAGE plpgsql;

-- fdw_privs_are ( fdw, user, privileges[], description )
CREATE OR REPLACE FUNCTION fdw_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_fdw_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_fdw' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    FDW ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- fdw_privs_are ( fdw, user, privileges[] )
CREATE OR REPLACE FUNCTION fdw_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT fdw_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on FDW ' || quote_ident($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_schema_privs(NAME, TEXT)
RETURNS TEXT[] AS $$
DECLARE
    privs  TEXT[] := ARRAY['CREATE', 'USAGE'];
    grants TEXT[] := '{}';
BEGIN
    FOR i IN 1..array_upper(privs, 1) LOOP
        IF pg_catalog.has_schema_privilege($1, $2, privs[i]) THEN
            grants := grants || privs[i];
        END IF;
    END LOOP;
    RETURN grants;
EXCEPTION
    -- Not a valid schema name.
    WHEN invalid_schema_name THEN RETURN '{invalid_schema_name}';
    -- Not a valid role.
    WHEN undefined_object   THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _get_server_privs (NAME, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_server_privilege($1, $2, 'USAGE') THEN
        RETURN '{USAGE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION WHEN undefined_object THEN
    -- Same error code for unknown user or server. So figure out which.
    RETURN CASE WHEN SQLERRM LIKE '%' || $1 || '%' THEN
        '{undefined_role}'
    ELSE
        '{undefined_server}'
    END;
END;
$$ LANGUAGE plpgsql;

-- server_privs_are ( server, user, privileges[], description )
CREATE OR REPLACE FUNCTION server_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_server_privs( $2, quote_ident($1) );
BEGIN
    IF grants[1] = 'undefined_server' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Server ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- server_privs_are ( server, user, privileges[] )
CREATE OR REPLACE FUNCTION server_privs_are ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT server_privs_are(
        $1, $2, $3,
        'Role ' || quote_ident($2) || ' should be granted '
            || CASE WHEN $3[1] IS NULL THEN 'no privileges' ELSE array_to_string($3, ', ') END
            || ' on server ' || quote_ident($1)
    );
$$ LANGUAGE SQL;
