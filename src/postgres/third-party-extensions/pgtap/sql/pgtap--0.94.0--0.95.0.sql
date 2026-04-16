CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.95;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION plan( integer )
RETURNS TEXT AS $$
DECLARE
    rcount INTEGER;
BEGIN
    BEGIN
        EXECUTE '
            CREATE TEMP SEQUENCE __tcache___id_seq;
            CREATE TEMP TABLE __tcache__ (
                id    INTEGER NOT NULL DEFAULT nextval(''__tcache___id_seq''),
                label TEXT    NOT NULL,
                value INTEGER NOT NULL,
                note  TEXT    NOT NULL DEFAULT ''''
            );
            CREATE UNIQUE INDEX __tcache___key ON __tcache__(id);
            GRANT ALL ON TABLE __tcache__ TO PUBLIC;
            GRANT ALL ON TABLE __tcache___id_seq TO PUBLIC;

            CREATE TEMP SEQUENCE __tresults___numb_seq;
            GRANT ALL ON TABLE __tresults___numb_seq TO PUBLIC;
        ';

    EXCEPTION WHEN duplicate_table THEN
        -- Raise an exception if there's already a plan.
        EXECUTE 'SELECT TRUE FROM __tcache__ WHERE label = ''plan''';
      GET DIAGNOSTICS rcount = ROW_COUNT;
        IF rcount > 0 THEN
           RAISE EXCEPTION 'You tried to plan twice!';
        END IF;
    END;

    -- Save the plan and return.
    PERFORM _set('plan', $1 );
    PERFORM _set('failed', 0 );
    RETURN '1..' || $1;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION add_result ( bool, bool, text, text, text )
RETURNS integer AS $$
BEGIN
    IF NOT $1 THEN PERFORM _set('failed', _get('failed') + 1); END IF;
    RETURN nextval('__tresults___numb_seq');
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION num_failed ()
RETURNS INTEGER AS $$
    SELECT _get('failed');
$$ LANGUAGE SQL strict;

CREATE OR REPLACE FUNCTION _finish (INTEGER, INTEGER, INTEGER)
RETURNS SETOF TEXT AS $$
DECLARE
    curr_test ALIAS FOR $1;
    exp_tests INTEGER := $2;
    num_faild ALIAS FOR $3;
    plural    CHAR;
BEGIN
    plural    := CASE exp_tests WHEN 1 THEN '' ELSE 's' END;

    IF curr_test IS NULL THEN
        RAISE EXCEPTION '# No tests run!';
    END IF;

    IF exp_tests = 0 OR exp_tests IS NULL THEN
         -- No plan. Output one now.
        exp_tests = curr_test;
        RETURN NEXT '1..' || exp_tests;
    END IF;

    IF curr_test <> exp_tests THEN
        RETURN NEXT diag(
            'Looks like you planned ' || exp_tests || ' test' ||
            plural || ' but ran ' || curr_test
        );
    ELSIF num_faild > 0 THEN
        RETURN NEXT diag(
            'Looks like you failed ' || num_faild || ' test' ||
            CASE num_faild WHEN 1 THEN '' ELSE 's' END
            || ' of ' || exp_tests
        );
    ELSE

    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;

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

-- materialized_views_are( schema, materialized_views, description )
CREATE OR REPLACE FUNCTION materialized_views_are ( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'Materialized views', _extras('m', $1, $2), _missing('m', $1, $2), $3);
$$ LANGUAGE SQL;

-- materialized_views_are( materialized_views, description )
CREATE OR REPLACE FUNCTION materialized_views_are ( NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are( 'Materialized views', _extras('m', $1), _missing('m', $1), $2);
$$ LANGUAGE SQL;

-- materialized_views_are( schema, materialized_views )
CREATE OR REPLACE FUNCTION materialized_views_are ( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'Materialized views', _extras('m', $1, $2), _missing('m', $1, $2),
        'Schema ' || quote_ident($1) || ' should have the correct materialized views'
    );
$$ LANGUAGE SQL;

-- materialized_views_are( materialized_views )
CREATE OR REPLACE FUNCTION materialized_views_are ( NAME[] )
RETURNS TEXT AS $$
    SELECT _are(
        'Materialized views', _extras('m', $1), _missing('m', $1),
        'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct materialized views'
    );
$$ LANGUAGE SQL;

-- materialized_view_owner_is ( schema, materialized_view, user, description )
CREATE OR REPLACE FUNCTION materialized_view_owner_is ( NAME, NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('m'::char, $1, $2);
BEGIN
    -- Make sure the materialized view exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            E'    Materialized view ' || quote_ident($1) || '.' || quote_ident($2) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- materialized_view_owner_is ( schema, materialized_view, user )
CREATE OR REPLACE FUNCTION materialized_view_owner_is ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT materialized_view_owner_is(
        $1, $2, $3,
        'Materialized view ' || quote_ident($1) || '.' || quote_ident($2) || ' should be owned by ' || quote_ident($3)
    );
$$ LANGUAGE sql;

-- materialized_view_owner_is ( materialized_view, user, description )
CREATE OR REPLACE FUNCTION materialized_view_owner_is ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
DECLARE
    owner NAME := _get_rel_owner('m'::char, $1);
BEGIN
    -- Make sure the materialized view exists.
    IF owner IS NULL THEN
        RETURN ok(FALSE, $3) || E'\n' || diag(
            E'    Materialized view ' || quote_ident($1) || ' does not exist'
        );
    END IF;

    RETURN is(owner, $2, $3);
END;
$$ LANGUAGE plpgsql;

-- materialized_view_owner_is ( materialized_view, user )
CREATE OR REPLACE FUNCTION materialized_view_owner_is ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT materialized_view_owner_is(
        $1, $2,
        'Materialized view ' || quote_ident($1) || ' should be owned by ' || quote_ident($2)
    );
$$ LANGUAGE sql;

-- has_materialized_view( schema, materialized_view, description )
CREATE OR REPLACE FUNCTION has_materialized_view ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'm', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- has_materialized_view( materialized_view, description )
CREATE OR REPLACE FUNCTION has_materialized_view ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( _rexists( 'm', $1 ), $2 );
$$ LANGUAGE SQL;

-- has_materialized_view( materialized_view )
CREATE OR REPLACE FUNCTION has_materialized_view ( NAME )
RETURNS TEXT AS $$
    SELECT has_materialized_view( $1, 'Materialized view ' || quote_ident($1) || ' should exist' );
$$ LANGUAGE SQL;

-- hasnt_materialized_view( schema, materialized_view, description )
CREATE OR REPLACE FUNCTION hasnt_materialized_view ( NAME, NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'm', $1, $2 ), $3 );
$$ LANGUAGE SQL;

-- hasnt_materialized_view( materialized_view, description )
CREATE OR REPLACE FUNCTION hasnt_materialized_view ( NAME, TEXT )
RETURNS TEXT AS $$
    SELECT ok( NOT _rexists( 'm', $1 ), $2 );
$$ LANGUAGE SQL;

-- hasnt_materialized_view( materialized_view )
CREATE OR REPLACE FUNCTION hasnt_materialized_view ( NAME )
RETURNS TEXT AS $$
    SELECT hasnt_materialized_view( $1, 'Materialized view ' || quote_ident($1) || ' should not exist' );
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

CREATE OR REPLACE FUNCTION _runner( text[], text[], text[], text[], text[] )
RETURNS SETOF TEXT AS $$
DECLARE
    startup  ALIAS FOR $1;
    shutdown ALIAS FOR $2;
    setup    ALIAS FOR $3;
    teardown ALIAS FOR $4;
    tests    ALIAS FOR $5;
    tap      TEXT;
    tfaild   INTEGER := 0;
    ffaild   INTEGER := 0;
    tnumb    INTEGER := 0;
    fnumb    INTEGER := 0;
    tok      BOOLEAN := TRUE;
    errmsg   TEXT;
BEGIN
    BEGIN
        -- No plan support.
        PERFORM * FROM no_plan();
        FOR tap IN SELECT * FROM _runem(startup, false) LOOP RETURN NEXT tap; END LOOP;
    EXCEPTION
        -- Catch all exceptions and simply rethrow custom exceptions. This
        -- will roll back everything in the above block.
        WHEN raise_exception THEN RAISE EXCEPTION '%', SQLERRM;
    END;

    -- Record how startup tests have failed.
    tfaild := num_failed();

    FOR i IN 1..COALESCE(array_upper(tests, 1), 0) LOOP
        -- What subtest are we running?
        RETURN NEXT '    ' || diag_test_name('Subtest: ' || tests[i]);

        -- Reset the results.
        tok := TRUE;
        tnumb := COALESCE(_get('curr_test'), 0);

        IF tnumb > 0 THEN
            EXECUTE 'ALTER SEQUENCE __tresults___numb_seq RESTART WITH 1';
            PERFORM _set('curr_test', 0);
            PERFORM _set('failed', 0);
        END IF;

        BEGIN
            BEGIN
                -- Run the setup functions.
                FOR tap IN SELECT * FROM _runem(setup, false) LOOP
                    RETURN NEXT regexp_replace(tap, '^', '    ', 'gn');
                END LOOP;

                -- Run the actual test function.
                FOR tap IN EXECUTE 'SELECT * FROM ' || tests[i] || '()' LOOP
                    RETURN NEXT regexp_replace(tap, '^', '    ', 'gn');
                END LOOP;

                -- Run the teardown functions.
                FOR tap IN SELECT * FROM _runem(teardown, false) LOOP
                    RETURN NEXT regexp_replace(tap, '^', '    ', 'gn');
                END LOOP;

                -- Emit the plan.
                fnumb := COALESCE(_get('curr_test'), 0);
                RETURN NEXT '    1..' || fnumb;

                -- Emit any error messages.
                IF fnumb = 0 THEN
                    RETURN NEXT '    # No tests run!';
                    tok = false;
                ELSE
                    -- Report failures.
                    ffaild := num_failed();
                    IF ffaild > 0 THEN
                        tok := FALSE;
                        RETURN NEXT '    ' || diag(
                            'Looks like you failed ' || ffaild || ' test' ||
                             CASE tfaild WHEN 1 THEN '' ELSE 's' END
                             || ' of ' || fnumb
                        );
                    END IF;
                END IF;

            EXCEPTION WHEN raise_exception THEN
                -- Something went wrong. Record that fact.
                errmsg := SQLERRM;
            END;

            -- Always raise an exception to rollback any changes.
            RAISE EXCEPTION '__TAP_ROLLBACK__';

        EXCEPTION WHEN raise_exception THEN
            IF errmsg IS NOT NULL THEN
                -- Something went wrong. Emit the error message.
                tok := FALSE;
                RETURN NEXT '    ' || diag('Test died: ' || errmsg);
                errmsg := NULL;
            END IF;
       END;

        -- Restore the sequence.
        EXECUTE 'ALTER SEQUENCE __tresults___numb_seq RESTART WITH ' || tnumb + 1;
        PERFORM _set('curr_test', tnumb);
        PERFORM _set('failed', tfaild);

        -- Record this test.
        RETURN NEXT ok(tok, tests[i]);
        IF NOT tok THEN tfaild := tfaild + 1; END IF;

    END LOOP;

    -- Run the shutdown functions.
    FOR tap IN SELECT * FROM _runem(shutdown, false) LOOP RETURN NEXT tap; END LOOP;

    -- Finish up.
    FOR tap IN SELECT * FROM _finish( COALESCE(_get('curr_test'), 0), 0, tfaild ) LOOP
        RETURN NEXT tap;
    END LOOP;

    -- Clean up and return.
    PERFORM _cleanup();
    RETURN;
END;
$$ LANGUAGE plpgsql;

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
    aok    := substring(have, 1, 2) = 'ok';
    adescr := COALESCE(substring(have FROM  E'(?:not )?ok [[:digit:]]+ - ([^\n]+)'), '');

    -- Now delete those results.
    EXECUTE 'ALTER SEQUENCE __tresults___numb_seq RESTART WITH ' || tnumb;
    IF NOT aok THEN PERFORM _set('failed', _get('failed') - 1); END IF;

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

CREATE OR REPLACE FUNCTION _cleanup()
RETURNS boolean AS $$
    DROP SEQUENCE __tresults___numb_seq;
    DROP TABLE __tcache__;
    DROP SEQUENCE __tcache___id_seq;
    SELECT TRUE;
$$ LANGUAGE sql;

-- has_sequence( schema, sequence )
CREATE OR REPLACE FUNCTION has_sequence ( NAME, NAME )
RETURNS TEXT AS $$
    SELECT ok(
        _rexists( 'S', $1, $2 ),
        'Sequence ' || quote_ident($1) || '.' || quote_ident($2) || ' should exist'
    );
$$ LANGUAGE SQL;

GRANT SELECT ON tap_funky           TO PUBLIC;
GRANT SELECT ON pg_all_foreign_keys TO PUBLIC;
