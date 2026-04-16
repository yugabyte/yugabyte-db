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

-- table_owner_is ( table, user, description )
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

-- _partof( child_schema, child_table, parent_schema, parent_table )
CREATE OR REPLACE FUNCTION _partof ( NAME, NAME, NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_namespace cn
          JOIN pg_catalog.pg_class cc ON cn.oid = cc.relnamespace
          JOIN pg_catalog.pg_inherits i ON cc.oid = i.inhrelid
          JOIN pg_catalog.pg_class pc ON i.inhparent = pc.oid
          JOIN pg_catalog.pg_namespace pn ON pc.relnamespace = pn.oid
         WHERE cn.nspname = $1
           AND cc.relname = $2
           AND cc.relispartition
           AND pn.nspname = $3
           AND pc.relname = $4
           AND pc.relkind = 'p'
    )
$$ LANGUAGE sql;

-- _partof( child_table, parent_table )
CREATE OR REPLACE FUNCTION _partof ( NAME, NAME )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT true
          FROM pg_catalog.pg_class cc
          JOIN pg_catalog.pg_inherits i ON cc.oid = i.inhrelid
          JOIN pg_catalog.pg_class pc ON i.inhparent = pc.oid
         WHERE cc.relname = $1
           AND cc.relispartition
           AND pc.relname = $2
           AND pc.relkind = 'p'
           AND pg_catalog.pg_table_is_visible(cc.oid)
           AND pg_catalog.pg_table_is_visible(pc.oid)
    )
$$ LANGUAGE sql;

-- is_partition_of( child_schema, child_table, parent_schema, parent_table, description )
CREATE OR REPLACE FUNCTION is_partition_of ( NAME, NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _partof($1, $2, $3, $4), $5);
$$ LANGUAGE sql;

-- is_partition_of( child_schema, child_table, parent_schema, parent_table )
CREATE OR REPLACE FUNCTION is_partition_of ( NAME, NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _partof($1, $2, $3, $4),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should be a partition of '
        || quote_ident($3) || '.' || quote_ident($4)
    );
$$ LANGUAGE sql;

-- is_partition_of( child_table, parent_table, description )
CREATE OR REPLACE FUNCTION is_partition_of ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _partof($1, $2), $3);
$$ LANGUAGE sql;

-- is_partition_of( child_table, parent_table )
CREATE OR REPLACE FUNCTION is_partition_of ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _partof($1, $2),
        'Table ' || quote_ident($1) || ' should be a partition of ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- _parts(schema, table)
CREATE OR REPLACE FUNCTION _parts( NAME, NAME )
RETURNS SETOF NAME AS $$
    SELECT i.inhrelid::regclass::name
      FROM pg_catalog.pg_namespace n
      JOIN pg_catalog.pg_class c ON n.oid = c.relnamespace
      JOIN pg_catalog.pg_inherits i ON c.oid = i.inhparent
     WHERE n.nspname = $1
       AND c.relname = $2
       AND c.relkind = 'p'
$$ LANGUAGE SQL;

-- _parts(table)
CREATE OR REPLACE FUNCTION _parts( NAME )
RETURNS SETOF NAME AS $$
    SELECT i.inhrelid::regclass::name
      FROM pg_catalog.pg_class c
      JOIN pg_catalog.pg_inherits i ON c.oid = i.inhparent
     WHERE c.relname = $1
       AND c.relkind = 'p'
       AND pg_catalog.pg_table_is_visible(c.oid)
$$ LANGUAGE SQL;

-- partitions_are( schema, table, partitions, description )
CREATE OR REPLACE FUNCTION partitions_are( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'partitions',
        ARRAY(SELECT _parts($1, $2) EXCEPT SELECT unnest($3)),
        ARRAY(SELECT unnest($3) EXCEPT SELECT _parts($1, $2)),
        $4
    );
$$ LANGUAGE SQL;

-- partitions_are( schema, table, partitions )
CREATE OR REPLACE FUNCTION partitions_are( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT partitions_are(
        $1, $2, $3,
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should have the correct partitions'
    );
$$ LANGUAGE SQL;

-- partitions_are( table, partitions, description )
CREATE OR REPLACE FUNCTION partitions_are( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'partitions',
        ARRAY(SELECT _parts($1) EXCEPT SELECT unnest($2)),
        ARRAY(SELECT unnest($2) EXCEPT SELECT _parts($1)),
        $3
    );
$$ LANGUAGE SQL;

-- partitions_are( table, partitions )
CREATE OR REPLACE FUNCTION partitions_are( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT partitions_are(
        $1, $2,
        'Table ' || quote_ident($1) || ' should have the correct partitions'
    );
$$ LANGUAGE SQL;
