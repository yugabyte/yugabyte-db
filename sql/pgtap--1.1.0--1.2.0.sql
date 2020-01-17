CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 1.2;'
LANGUAGE SQL IMMUTABLE;

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
