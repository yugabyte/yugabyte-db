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
