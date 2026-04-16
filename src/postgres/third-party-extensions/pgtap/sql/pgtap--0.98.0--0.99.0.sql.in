CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.99;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION pg_version_num()
RETURNS integer AS $$
    SELECT current_setting('server_version_num')::integer;
$$ LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION _ident_array_to_sorted_string( name[], text )
RETURNS text AS $$
    SELECT array_to_string(ARRAY(
        SELECT quote_ident($1[i])
          FROM generate_series(1, array_upper($1, 1)) s(i)
         ORDER BY $1[i]
    ), $2);
$$ LANGUAGE SQL immutable;

CREATE OR REPLACE FUNCTION _array_to_sorted_string( name[], text )
RETURNS text AS $$
    SELECT array_to_string(ARRAY(
        SELECT $1[i]
          FROM generate_series(1, array_upper($1, 1)) s(i)
         ORDER BY $1[i]
    ), $2);
$$ LANGUAGE SQL immutable;

CREATE OR REPLACE FUNCTION _are ( text, name[], name[], TEXT )
RETURNS TEXT AS $$
DECLARE
    what    ALIAS FOR $1;
    extras  ALIAS FOR $2;
    missing ALIAS FOR $3;
    descr   ALIAS FOR $4;
    msg     TEXT    := '';
    res     BOOLEAN := TRUE;
BEGIN
    IF extras[1] IS NOT NULL THEN
        res = FALSE;
        msg := E'\n' || diag(
            '    Extra ' || what || E':\n        '
            ||  _ident_array_to_sorted_string( extras, E'\n        ' )
        );
    END IF;
    IF missing[1] IS NOT NULL THEN
        res = FALSE;
        msg := msg || E'\n' || diag(
            '    Missing ' || what || E':\n        '
            ||  _ident_array_to_sorted_string( missing, E'\n        ' )
        );
    END IF;

    RETURN ok(res, descr) || msg;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _areni ( text, text[], text[], TEXT )
RETURNS TEXT AS $$
DECLARE
    what    ALIAS FOR $1;
    extras  ALIAS FOR $2;
    missing ALIAS FOR $3;
    descr   ALIAS FOR $4;
    msg     TEXT    := '';
    res     BOOLEAN := TRUE;
BEGIN
    IF extras[1] IS NOT NULL THEN
        res = FALSE;
        msg := E'\n' || diag(
            '    Extra ' || what || E':\n        '
            ||  _array_to_sorted_string( extras, E'\n        ' )
        );
    END IF;
    IF missing[1] IS NOT NULL THEN
        res = FALSE;
        msg := msg || E'\n' || diag(
            '    Missing ' || what || E':\n        '
            ||  _array_to_sorted_string( missing, E'\n        ' )
        );
    END IF;

    RETURN ok(res, descr) || msg;
END;
$$ LANGUAGE plpgsql;

-- Note: this fixes a bug in the 97->98 upgrade script
-- table_owner_is ( table, user, description )
/*
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('{r,p}'::char[], $1);
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
*/

-- _hasc( schema, table, constraint_type )
CREATE OR REPLACE FUNCTION _hasc ( NAME, NAME, CHAR )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
            SELECT true
              FROM pg_catalog.pg_namespace n
              JOIN pg_catalog.pg_class c      ON c.relnamespace = n.oid
              JOIN pg_catalog.pg_constraint x ON c.oid = x.conrelid
              JOIN pg_catalog.pg_index i      ON c.oid = i.indrelid
             WHERE i.indisprimary = true
               AND n.nspname = $1
               AND c.relname = $2
               AND x.contype = $3
    );
$$ LANGUAGE sql;

-- _hasc( table, constraint_type )
CREATE OR REPLACE FUNCTION _hasc ( NAME, CHAR )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
            SELECT true
              FROM pg_catalog.pg_class c
              JOIN pg_catalog.pg_constraint x ON c.oid = x.conrelid
              JOIN pg_catalog.pg_index i      ON c.oid = i.indrelid
             WHERE i.indisprimary = true
               AND pg_table_is_visible(c.oid)
               AND c.relname = $1
               AND x.contype = $2
    );
$$ LANGUAGE sql;

CREATE OR REPLACE VIEW tap_funky
 AS SELECT p.oid         AS oid,
           n.nspname     AS schema,
           p.proname     AS name,
           pg_catalog.pg_get_userbyid(p.proowner) AS owner,
           array_to_string(p.proargtypes::regtype[], ',') AS args,
           CASE p.proretset WHEN TRUE THEN 'setof ' ELSE '' END
             || p.prorettype::regtype AS returns,
           p.prolang     AS langoid,
           p.proisstrict AS is_strict,
           p.prokind     AS kind,
           p.prosecdef   AS is_definer,
           p.proretset   AS returns_set,
           p.provolatile::char AS volatility,
           pg_catalog.pg_function_is_visible(p.oid) AS is_visible
      FROM pg_catalog.pg_proc p
      JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid
;

CREATE OR REPLACE FUNCTION _agg ( NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = 'a'
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = array_to_string($3, ',')
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _agg ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT kind = 'a' FROM tap_funky WHERE schema = $1 AND name = $2
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _agg ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = 'a'
      FROM tap_funky
     WHERE name = $1
       AND args = array_to_string($2, ',')
       AND is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _agg ( NAME )
RETURNS BOOLEAN AS $$
    SELECT kind = 'a' FROM tap_funky WHERE name = $1 AND is_visible;
$$ LANGUAGE SQL;

-- _hasc( schema, table, constraint_type )
CREATE OR REPLACE FUNCTION _hasc ( NAME, NAME, CHAR )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
            SELECT true
              FROM pg_catalog.pg_namespace n
              JOIN pg_catalog.pg_class c      ON c.relnamespace = n.oid
              JOIN pg_catalog.pg_constraint x ON c.oid = x.conrelid
             WHERE n.nspname = $1
               AND c.relname = $2
               AND x.contype = $3
    );
$$ LANGUAGE sql;

-- _hasc( table, constraint_type )
CREATE OR REPLACE FUNCTION _hasc ( NAME, CHAR )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
            SELECT true
              FROM pg_catalog.pg_class c
              JOIN pg_catalog.pg_constraint x ON c.oid = x.conrelid
             WHERE pg_table_is_visible(c.oid)
               AND c.relname = $1
               AND x.contype = $2
    );
$$ LANGUAGE sql;
