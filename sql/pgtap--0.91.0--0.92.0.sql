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
