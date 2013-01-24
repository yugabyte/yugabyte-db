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

CREATE OR REPLACE FUNCTION _get_col_type ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT display_type(a.atttypid, a.atttypmod)
      FROM pg_catalog.pg_attribute a
      JOIN pg_catalog.pg_class c ON  a.attrelid = c.oid
     WHERE pg_catalog.pg_table_is_visible(c.oid)
       AND c.relname = $1
       AND a.attname = $2
       AND attnum    > 0
       AND NOT a.attisdropped
$$ LANGUAGE SQL;
