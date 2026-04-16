CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.94;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION _get_func_privs(TEXT, TEXT)
RETURNS TEXT[] AS $$
BEGIN
    IF pg_catalog.has_function_privilege($1, $2, 'EXECUTE') THEN
        RETURN '{EXECUTE}';
    ELSE
        RETURN '{}';
    END IF;
EXCEPTION
    -- Not a valid func name.
    WHEN undefined_function THEN RETURN '{undefined_function}';
    -- Not a valid role.
    WHEN undefined_object   THEN RETURN '{undefined_role}';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION _fprivs_are ( TEXT, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_func_privs($2, $1);
BEGIN
    IF grants[1] = 'undefined_function' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Function ' || $1 || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- has_table( schema, table )
CREATE OR REPLACE FUNCTION has_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists( 'r', $1, $2 ),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should exist'
    );
$$ LANGUAGE SQL;

-- hasnt_table( schema, table )
CREATE OR REPLACE FUNCTION hasnt_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _rexists( 'r', $1, $2 ),
        'Table ' || quote_ident($1) || '.' || quote_ident($2) || ' should not exist'
    );
$$ LANGUAGE SQL;

-- has_foreign_table( schema, table )
CREATE OR REPLACE FUNCTION has_foreign_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists( 'f', $1, $2 ),
        'Foreign table ' || quote_ident($1) || '.' || quote_ident($2) || ' should exist'
    );
$$ LANGUAGE SQL;

-- hasnt_foreign_table( schema, table )
CREATE OR REPLACE FUNCTION hasnt_foreign_table ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        NOT _rexists( 'f', $1, $2 ),
        'Foreign table ' || quote_ident($1) || '.' || quote_ident($2) || ' not should exist'
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION skip ( why text, how_many int )
RETURNS TEXT AS $$
DECLARE
    output TEXT[];
BEGIN
    output := '{}';
    FOR i IN 1..how_many LOOP
        output = array_append(
            output,
            ok( TRUE ) || ' ' || diag( 'SKIP' || COALESCE( ' ' || why, '') )
        );
    END LOOP;
    RETURN array_to_string(output, E'\n');
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION skip ( text )
RETURNS TEXT AS $$
    SELECT ok( TRUE ) || ' ' || diag( 'SKIP' || COALESCE(' ' || $1, '') );
$$ LANGUAGE sql;

-- check_test( test_output, pass, name, description, diag, match_diag )
CREATE OR REPLACE FUNCTION check_test( TEXT, BOOLEAN, TEXT, TEXT, TEXT, BOOLEAN )
RETURNS SETOF TEXT AS $$
DECLARE
    tnumb   INTEGER;
    aok     BOOLEAN;
    adescr  TEXT;
    res     BOOLEAN;
    descr   TEXT;
    adiag   TEXT;
    have    ALIAS FOR $1;
    eok     ALIAS FOR $2;
    name    ALIAS FOR $3;
    edescr  ALIAS FOR $4;
    ediag   ALIAS FOR $5;
    matchit ALIAS FOR $6;
BEGIN
    -- What test was it that just ran?
    tnumb := currval('__tresults___numb_seq');

    -- Fetch the results.
    EXECUTE 'SELECT aok, descr FROM __tresults__ WHERE numb = ' || tnumb
       INTO aok, adescr;

    -- Now delete those results.
    EXECUTE 'DELETE FROM __tresults__ WHERE numb = ' || tnumb;
    EXECUTE 'ALTER SEQUENCE __tresults___numb_seq RESTART WITH ' || tnumb;

    -- Set up the description.
    descr := coalesce( name || ' ', 'Test ' ) || 'should ';

    -- So, did the test pass?
    RETURN NEXT is(
        aok,
        eok,
        descr || CASE eok WHEN true then 'pass' ELSE 'fail' END
    );

    -- Was the description as expected?
    IF edescr IS NOT NULL THEN
        RETURN NEXT is(
            adescr,
            edescr,
            descr || 'have the proper description'
        );
    END IF;

    -- Were the diagnostics as expected?
    IF ediag IS NOT NULL THEN
        -- Remove ok and the test number.
        adiag := substring(
            have
            FROM CASE WHEN aok THEN 4 ELSE 9 END + char_length(tnumb::text)
        );

        -- Remove the description, if there is one.
        IF adescr <> '' THEN
            adiag := substring(
                adiag FROM 1 + char_length( ' - ' || substr(diag( adescr ), 3) )
            );
        END IF;

        IF NOT aok THEN
            -- Remove failure message from ok().
            adiag := substring(adiag FROM 1 + char_length(diag(
                'Failed test ' || tnumb ||
                CASE adescr WHEN '' THEN '' ELSE COALESCE(': "' || adescr || '"', '') END
            )));
        END IF;

        IF ediag <> '' THEN
           -- Remove the space before the diagnostics.
           adiag := substring(adiag FROM 2);
        END IF;

        -- Remove the #s.
        adiag := replace( substring(adiag from 3), E'\n# ', E'\n' );

        -- Now compare the diagnostics.
        IF matchit THEN
            RETURN NEXT matches(
                adiag,
                ediag,
                descr || 'have the proper diagnostics'
            );
        ELSE
            RETURN NEXT is(
                adiag,
                ediag,
                descr || 'have the proper diagnostics'
            );
        END IF;
    END IF;

    -- And we're done
    RETURN;
END;
$$ LANGUAGE plpgsql;
