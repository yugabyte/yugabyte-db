CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.95;'
LANGUAGE SQL IMMUTABLE;

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

CREATE OR REPLACE FUNCTION _fix_enum_has_labels(
) RETURNS  VOID LANGUAGE PLPGSQL AS $FIX$
BEGIN
    IF pg_version_num() < 90100 THEN RETURN; END IF;
    EXECUTE $RUN$
        -- enum_has_labels( schema, enum, labels, description )
        CREATE OR REPLACE FUNCTION enum_has_labels( NAME, NAME, NAME[], TEXT )
        RETURNS TEXT AS $$
            SELECT is(
                ARRAY(
                    SELECT e.enumlabel
                      FROM pg_catalog.pg_type t
                      JOIN pg_catalog.pg_enum e      ON t.oid = e.enumtypid
                      JOIN pg_catalog.pg_namespace n ON t.typnamespace = n.oid
                      WHERE t.typisdefined
                       AND n.nspname = $1
                       AND t.typname = $2
                       AND t.typtype = 'e'
                     ORDER BY e.enumsortorder
                ),
                $3,
                $4
            );
        $$ LANGUAGE sql;

        -- enum_has_labels( enum, labels, description )
        CREATE OR REPLACE FUNCTION enum_has_labels( NAME, NAME[], TEXT )
        RETURNS TEXT AS $$
            SELECT is(
                ARRAY(
                    SELECT e.enumlabel
                      FROM pg_catalog.pg_type t
                      JOIN pg_catalog.pg_enum e ON t.oid = e.enumtypid
                      WHERE t.typisdefined
                       AND pg_catalog.pg_type_is_visible(t.oid)
                       AND t.typname = $1
                       AND t.typtype = 'e'
                     ORDER BY e.enumsortorder
                ),
                $2,
                $3
            );
        $$ LANGUAGE sql;
    $RUN$;
END;
$FIX$;

SELECT _fix_enum_has_labels();
DROP FUNCTION _fix_enum_has_labels();

-- isnt_strict( schema, function, args[], description )
CREATE OR REPLACE FUNCTION isnt_strict ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, $3, NOT _strict($1, $2, $3), $4 );
$$ LANGUAGE SQL;

-- isnt_strict( schema, function, args[] )
CREATE OR REPLACE FUNCTION isnt_strict( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _strict($1, $2, $3),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '(' ||
        array_to_string($3, ', ') || ') should not be strict'
    );
$$ LANGUAGE sql;

-- isnt_strict( schema, function, description )
CREATE OR REPLACE FUNCTION isnt_strict ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare($1, $2, NOT _strict($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_strict( schema, function )
CREATE OR REPLACE FUNCTION isnt_strict( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _strict($1, $2),
        'Function ' || quote_ident($1) || '.' || quote_ident($2) || '() should not be strict'
    );
$$ LANGUAGE sql;

-- isnt_strict( function, args[], description )
CREATE OR REPLACE FUNCTION isnt_strict ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, $2, NOT _strict($1, $2), $3 );
$$ LANGUAGE SQL;

-- isnt_strict( function, args[] )
CREATE OR REPLACE FUNCTION isnt_strict( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _strict($1, $2),
        'Function ' || quote_ident($1) || '(' ||
        array_to_string($2, ', ') || ') should not be strict'
    );
$$ LANGUAGE sql;

-- isnt_strict( function, description )
CREATE OR REPLACE FUNCTION isnt_strict( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT _func_compare(NULL, $1, NOT _strict($1), $2 );
$$ LANGUAGE sql;

-- isnt_strict( function )
CREATE OR REPLACE FUNCTION isnt_strict( NAME )
RETURNS TEXT AS $$
    SELECT ok( NOT _strict($1), 'Function ' || quote_ident($1) || '() should not be strict' );
$$ LANGUAGE sql;

-- col_is_unique( schema, table, column[] )
CREATE OR REPLACE FUNCTION col_is_unique ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT col_is_unique( $1, $2, $3, 'Columns ' || quote_ident($2) || '(' || _ident_array_to_string($3, ', ') || ') should have a unique constraint' );
$$ LANGUAGE sql;

-- col_is_unique( scheam, table, column )
CREATE OR REPLACE FUNCTION col_is_unique ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT col_is_unique( $1, $2, ARRAY[$3], 'Column ' || quote_ident($2) || '(' || quote_ident($3) || ') should have a unique constraint' );
$$ LANGUAGE sql;
