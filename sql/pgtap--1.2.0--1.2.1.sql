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
        RETURN NEXT diag_test_name('Subtest: ' || tests[i]);

        -- Reset the results.
        tok := TRUE;
        tnumb := COALESCE(_get('curr_test'), 0);

        IF tnumb > 0 THEN
            EXECUTE 'ALTER SEQUENCE __tresults___numb_seq RESTART WITH 1';
            PERFORM _set('curr_test', 0);
            PERFORM _set('failed', 0);
        END IF;

        DECLARE
            errstate text;
            errmsg   text;
            detail   text;
            hint     text;
            context  text;
            schname  text;
            tabname  text;
            colname  text;
            chkname  text;
            typname  text;
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
                             CASE ffaild WHEN 1 THEN '' ELSE 's' END
                             || ' of ' || fnumb
                        );
                    END IF;
                END IF;

            EXCEPTION WHEN OTHERS THEN
                -- Something went wrong. Record that fact.
                errstate := SQLSTATE;
                errmsg := SQLERRM;
                GET STACKED DIAGNOSTICS
                    detail  = PG_EXCEPTION_DETAIL,
                    hint    = PG_EXCEPTION_HINT,
                    context = PG_EXCEPTION_CONTEXT,
                    schname = SCHEMA_NAME,
                    tabname = TABLE_NAME,
                    colname = COLUMN_NAME,
                    chkname = CONSTRAINT_NAME,
                    typname = PG_DATATYPE_NAME;
            END;

            -- Always raise an exception to rollback any changes.
            RAISE EXCEPTION '__TAP_ROLLBACK__';

        EXCEPTION WHEN raise_exception THEN
            IF errmsg IS NOT NULL THEN
                -- Something went wrong. Emit the error message.
                tok := FALSE;
               RETURN NEXT regexp_replace( diag('Test died: ' || _error_diag(
                   errstate, errmsg, detail, hint, context, schname, tabname, colname, chkname, typname
               )), '^', '    ', 'gn');
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

-- col_is_pk( schema, table, column[] )
CREATE OR REPLACE FUNCTION col_is_pk ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT col_is_pk( $1, $2, $3, 'Columns ' || quote_ident($1) || '.' || quote_ident($2) || '(' || _ident_array_to_string($3, ', ') || ') should be a primary key' );
$$ LANGUAGE sql;

-- col_is_pk( schema, table, column )
CREATE OR REPLACE FUNCTION col_is_pk ( NAME, NAME, NAME )
RETURNS TEXT AS $$
    SELECT col_is_pk( $1, $2, $3, 'Column ' || quote_ident($1) || '.' || quote_ident($2) || '(' || quote_ident($3) || ') should be a primary key' );
$$ LANGUAGE sql;

-- schemas_are( schemas, description )
CREATE OR REPLACE FUNCTION schemas_are ( NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'schemas',
        ARRAY(
            SELECT nspname
              FROM pg_catalog.pg_namespace
             WHERE nspname NOT LIKE 'pg\_%'
               AND nspname <> 'information_schema'
             EXCEPT
            SELECT $1[i]
              FROM generate_series(1, array_upper($1, 1)) s(i)
        ),
        ARRAY(
            SELECT $1[i]
              FROM generate_series(1, array_upper($1, 1)) s(i)
            EXCEPT
            SELECT nspname
              FROM pg_catalog.pg_namespace
             WHERE nspname NOT LIKE 'pg\_%'
               AND nspname <> 'information_schema'
        ),
        $2
    );
CREATE OR REPLACE FUNCTION _lang ( NAME, NAME, NAME[] )
RETURNS NAME AS $$
    SELECT l.lanname
      FROM tap_funky f
      JOIN pg_catalog.pg_language l ON f.langoid = l.oid
     WHERE f.schema = $1
       and f.name   = $2
       AND f.args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _lang ( NAME, NAME[] )
RETURNS NAME AS $$
    SELECT l.lanname
      FROM tap_funky f
      JOIN pg_catalog.pg_language l ON f.langoid = l.oid
     WHERE f.name = $1
       AND f.args = _funkargs($2)
       AND f.is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _returns ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT returns
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _returns ( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT returns
      FROM tap_funky
     WHERE name = $1
       AND args = _funkargs($2)
       AND is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _definer ( NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT is_definer
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _definer ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT is_definer
      FROM tap_funky
     WHERE name = $1
       AND args = _funkargs($2)
       AND is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _type_func ( "char", NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = $1
      FROM tap_funky
     WHERE schema = $2
       AND name   = $3
       AND args   = _funkargs($4)
$$ LANGUAGE SQL;

-- _type_func(type, function, args[])
CREATE OR REPLACE FUNCTION _type_func ( "char", NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT kind = $1
      FROM tap_funky
     WHERE name = $2
       AND args = _funkargs($3)
       AND is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _strict ( NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT is_strict
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _strict ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT is_strict
      FROM tap_funky
     WHERE name = $1
       AND args = _funkargs($2)
       AND is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _vol ( NAME, NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _expand_vol(volatility)
      FROM tap_funky f
     WHERE f.schema = $1
       and f.name   = $2
       AND f.args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _vol ( NAME, NAME[] )
RETURNS TEXT AS $$
    SELECT _expand_vol(volatility)
      FROM tap_funky f
     WHERE f.name = $1
       AND f.args = _funkargs($2)
       AND f.is_visible;
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_func_owner ( NAME, NAME, NAME[] )
RETURNS NAME AS $$
    SELECT owner
      FROM tap_funky
     WHERE schema = $1
       AND name   = $2
       AND args   = _funkargs($3)
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _get_func_owner ( NAME, NAME[] )
RETURNS NAME AS $$
    SELECT owner
      FROM tap_funky
     WHERE name = $1
       AND args = _funkargs($2)
       AND is_visible
$$ LANGUAGE SQL;
