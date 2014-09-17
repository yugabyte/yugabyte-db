-- is_member_of( role, members[], description )
CREATE OR REPLACE FUNCTION is_member_of( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    missing text[];
BEGIN
    IF NOT _has_role($1) THEN
        RETURN fail( $3 ) || E'\n' || diag (
            '    Role ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    SELECT ARRAY(
        SELECT quote_ident($2[i])
          FROM generate_series(1, array_upper($2, 1)) s(i)
          LEFT JOIN pg_catalog.pg_roles r ON rolname = $2[i]
         WHERE r.oid IS NULL
            OR NOT r.oid = ANY ( _grolist($1) )
         ORDER BY s.i
    ) INTO missing;
    IF missing[1] IS NULL THEN
        RETURN ok( true, $3 );
    END IF;
    RETURN ok( false, $3 ) || E'\n' || diag(
        '    Members missing from the ' || quote_ident($1) || E' role:\n        ' ||
        array_to_string( missing, E'\n        ')
    );
END;
$$ LANGUAGE plpgsql;

-- is_member_of( role, members[] )
CREATE OR REPLACE FUNCTION is_member_of( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT is_member_of( $1, $2, 'Should have members of role ' || quote_ident($1) );
$$ LANGUAGE SQL;

-- foreign_tables_are( schema, tables, description )
CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'foreign tables', _extras('f', $1, $2), _missing('f', $1, $2), $3);
$$ LANGUAGE SQL;

-- foreign_tables_are( tables, description )
CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'foreign tables', _extras('f', $1), _missing('f', $1), $2);
$$ LANGUAGE SQL;

-- foreign_tables_are( schema, tables )
CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'foreign tables', _extras('f', $1, $2), _missing('f', $1, $2),
        'Schema ' || quote_ident($1) || ' should have the correct foreign tables'
    );
$$ LANGUAGE SQL;

-- foreign_tables_are( tables )
CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'foreign tables', _extras('f', $1), _missing('f', $1),
        'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct foreign tables'
    );
$$ LANGUAGE SQL;
