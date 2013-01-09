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
