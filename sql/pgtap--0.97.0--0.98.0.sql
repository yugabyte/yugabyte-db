CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.98;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION _rexists ( CHAR[], NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE c.relkind = ANY($1)
           AND n.nspname = $2
           AND c.relname = $3
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _rexists ( CHAR[], NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_class c
         WHERE c.relkind = ANY($1)
           AND pg_catalog.pg_table_is_visible(c.oid)
           AND c.relname = $2
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _rexists ( CHAR, NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT _rexists(ARRAY[$1], $2, $3);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _rexists ( CHAR, NAME )
RETURNS BOOLEAN AS $$
SELECT _rexists(ARRAY[$1], $2);
$$ LANGUAGE SQL;

-- has_table( schema, table, description )
CREATE OR REPLACE FUNCTION has_table ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( '{r,p}'::char[], $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_table( schema, table )
CREATE OR REPLACE FUNCTION has_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists( '{r,p}'::char[], $1, $2 ),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should exist'
    );
$$ LANGUAGE SQL;

-- has_table( table, description )
CREATE OR REPLACE FUNCTION has_table ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( '{r,p}'::char[], $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_table( schema, table, description )
CREATE OR REPLACE FUNCTION hasnt_table ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( '{r,p}'::char[], $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_table( schema, table )
CREATE OR REPLACE FUNCTION hasnt_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _rexists( '{r,p}'::char[], $1, $2 ),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should not exist'
    );
$$ LANGUAGE SQL;

-- hasnt_table( table, description )
CREATE OR REPLACE FUNCTION hasnt_table ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( '{r,p}'::char[], $1 ), $2 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _extras ( CHAR[], NAME, NAME[] )
RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT c.relname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE c.relkind = ANY($1)
           AND n.nspname = $2
           AND c.relname NOT IN('pg_all_foreign_keys', 'tap_funky', '__tresults___numb_seq', '__tcache___id_seq')
        EXCEPT
        SELECT $3[i]
          FROM generate_series(1, array_upper($3, 1)) s(i)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _extras ( CHAR[], NAME[] )
RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT c.relname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE pg_catalog.pg_table_is_visible(c.oid)
           AND n.nspname <> 'pg_catalog'
           AND c.relkind = ANY($1)
           AND c.relname NOT IN ('__tcache__', 'pg_all_foreign_keys', 'tap_funky', '__tresults___numb_seq', '__tcache___id_seq')
        EXCEPT
        SELECT $2[i]
          FROM generate_series(1, array_upper($2, 1)) s(i)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _extras ( CHAR, NAME, NAME[] )
RETURNS NAME[] AS $$
    SELECT _extras(ARRAY[$1], $2, $3);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _extras ( CHAR, NAME[] )
RETURNS NAME[] AS $$
SELECT _extras(ARRAY[$1], $2);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _missing ( CHAR[], NAME, NAME[] )
RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT $3[i]
          FROM generate_series(1, array_upper($3, 1)) s(i)
        EXCEPT
        SELECT c.relname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE c.relkind = ANY($1)
           AND n.nspname = $2
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _missing ( CHAR[], NAME[] )
RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT $2[i]
          FROM generate_series(1, array_upper($2, 1)) s(i)
        EXCEPT
        SELECT c.relname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
         WHERE pg_catalog.pg_table_is_visible(c.oid)
           AND n.nspname NOT IN ('pg_catalog', 'information_schema')
           AND c.relkind = ANY($1)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _missing ( CHAR, NAME, NAME[] )
RETURNS NAME[] AS $$
    SELECT _missing(ARRAY[$1], $2, $3);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _missing ( CHAR, NAME[] )
RETURNS NAME[] AS $$
    SELECT _missing(ARRAY[$1], $2);
$$ LANGUAGE SQL;

-- tables_are( schema, tables, description )
CREATE OR REPLACE FUNCTION tables_are ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'tables', _extras('{r,p}'::char[], $1, $2), _missing('{r,p}'::char[], $1, $2), $3);
$$ LANGUAGE SQL;

-- tables_are( tables, description )
CREATE OR REPLACE FUNCTION tables_are ( NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'tables', _extras('{r,p}'::char[], $1), _missing('{r,p}'::char[], $1), $2);
$$ LANGUAGE SQL;

-- tables_are( schema, tables )
CREATE OR REPLACE FUNCTION tables_are ( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'tables', _extras('{r,p}'::char[], $1, $2), _missing('{r,p}'::char[], $1, $2),
        'Schema ' || quote_ident($1) || ' should have the correct tables'
    );
$$ LANGUAGE SQL;

-- tables_are( tables )
CREATE OR REPLACE FUNCTION tables_are ( NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'tables', _extras('{r,p}'::char[], $1), _missing('{r,p}'::char[], $1),
        'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables'
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR[], NAME, NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
      JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
     WHERE c.relkind = ANY($1)
       AND n.nspname = $2
       AND c.relname = $3
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR[], NAME )
RETURNS NAME AS $$
    SELECT pg_catalog.pg_get_userbyid(c.relowner)
      FROM pg_catalog.pg_class c
     WHERE c.relkind = ANY($1)
       AND c.relname = $2
       AND pg_catalog.pg_table_is_visible(c.oid)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR, NAME, NAME )
RETURNS NAME AS $$
    SELECT _get_rel_owner(ARRAY[$1], $2, $3);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_rel_owner ( CHAR, NAME )
RETURNS NAME AS $$
    SELECT _get_rel_owner(ARRAY[$1], $2);
$$ LANGUAGE SQL;

-- table_owner_is ( schema, table, user, description )
CREATE OR REPLACE FUNCTION table_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('{r,p}'::char[], $1, $2);
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

-- is_partitioned( schema, table, description )
CREATE OR REPLACE FUNCTION is_partitioned ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists('p', $1, $2), $3);
$$ LANGUAGE sql;

-- is_partitioned( schema, table )
CREATE OR REPLACE FUNCTION is_partitioned ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists('p', $1, $2),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should be partitioned'
    );
$$ LANGUAGE sql;

-- is_partitioned( table, description )
CREATE OR REPLACE FUNCTION is_partitioned ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists('p', $1), $2);
$$ LANGUAGE sql;

-- is_partitioned( table )
CREATE OR REPLACE FUNCTION is_partitioned ( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists('p', $1),
        'Table ' || quote_ident($1) || ' should be partitioned'
    );
$$ LANGUAGE sql;

-- isnt_partitioned( schema, table, description )
CREATE OR REPLACE FUNCTION isnt_partitioned ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists('p', $1, $2), $3);
$$ LANGUAGE sql;

-- isnt_partitioned( schema, table )
CREATE OR REPLACE FUNCTION isnt_partitioned ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _rexists('p', $1, $2),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should not be partitioned'
    );
$$ LANGUAGE sql;

-- isnt_partitioned( table, description )
CREATE OR REPLACE FUNCTION isnt_partitioned ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists('p', $1), $2);
$$ LANGUAGE sql;

-- isnt_partitioned( table )
CREATE OR REPLACE FUNCTION isnt_partitioned ( NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _rexists('p', $1),
        'Table ' || quote_ident($1) || ' should not be partitioned'
    );
$$ LANGUAGE sql;
