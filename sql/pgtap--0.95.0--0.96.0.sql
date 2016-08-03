CREATE OR REPLACE FUNCTION pgtap_version()
RETURNS NUMERIC AS 'SELECT 0.96;'
LANGUAGE SQL IMMUTABLE;

CREATE OR REPLACE FUNCTION findfuncs( NAME, TEXT, TEXT )
RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT DISTINCT quote_ident(n.nspname) || '.' || quote_ident(p.proname) AS pname
          FROM pg_catalog.pg_proc p
          JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid
         WHERE n.nspname = $1
           AND p.proname ~ $2
           AND ($3 IS NULL OR p.proname !~ $3)
         ORDER BY pname
    );
$$ LANGUAGE sql;
CREATE OR REPLACE FUNCTION findfuncs( NAME, TEXT )
RETURNS TEXT[] AS $$
    SELECT findfuncs( $1, $2, NULL )
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION findfuncs( TEXT, TEXT )
RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT DISTINCT quote_ident(n.nspname) || '.' || quote_ident(p.proname) AS pname
          FROM pg_catalog.pg_proc p
          JOIN pg_catalog.pg_namespace n ON p.pronamespace = n.oid
         WHERE pg_catalog.pg_function_is_visible(p.oid)
           AND p.proname ~ $1
           AND ($2 IS NULL OR p.proname !~ $2)
         ORDER BY pname
    );
$$ LANGUAGE sql;
CREATE OR REPLACE FUNCTION findfuncs( TEXT )
RETURNS TEXT[] AS $$
    SELECT findfuncs( $1, NULL )
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION runtests( NAME, TEXT )
RETURNS SETOF TEXT AS $$
    SELECT * FROM _runner(
        findfuncs( $1, '^startup' ),
        findfuncs( $1, '^shutdown' ),
        findfuncs( $1, '^setup' ),
        findfuncs( $1, '^teardown' ),
        findfuncs( $1, $2, '^(startup|shutdown|setup|teardown)' )
    );
$$ LANGUAGE sql;

-- runtests( match )
CREATE OR REPLACE FUNCTION runtests( TEXT )
RETURNS SETOF TEXT AS $$
    SELECT * FROM _runner(
        findfuncs( '^startup' ),
        findfuncs( '^shutdown' ),
        findfuncs( '^setup' ),
        findfuncs( '^teardown' ),
        findfuncs( $1, '^(startup|shutdown|setup|teardown)' )
    );
$$ LANGUAGE sql;

-- database_privs_are ( db, user, privileges[], description )
CREATE OR REPLACE FUNCTION database_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_db_privs( $2, $1::TEXT );
BEGIN
    IF grants[1] = 'invalid_catalog_name' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Database ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- schema_privs_are ( schema, user, privileges[], description )
CREATE OR REPLACE FUNCTION schema_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_schema_privs( $2, $1::TEXT );
BEGIN
    IF grants[1] = 'invalid_schema_name' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Schema ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- tablespace_privs_are ( tablespace, user, privileges[], description )
CREATE OR REPLACE FUNCTION tablespace_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_tablespaceprivs( $2, $1::TEXT );
BEGIN
    IF grants[1] = 'undefined_tablespace' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Tablespace ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION fdw_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_fdw_privs( $2, $1::TEXT );
BEGIN
    IF grants[1] = 'undefined_fdw' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    FDW ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- server_privs_are ( server, user, privileges[], description )
CREATE OR REPLACE FUNCTION server_privs_are ( NAME, NAME, NAME[], TEXT )
RETURNS TEXT AS $$
DECLARE
    grants TEXT[] := _get_server_privs( $2, $1::TEXT );
BEGIN
    IF grants[1] = 'undefined_server' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Server ' || quote_ident($1) || ' does not exist'
        );
    ELSIF grants[1] = 'undefined_role' THEN
        RETURN ok(FALSE, $4) || E'\n' || diag(
            '    Role ' || quote_ident($2) || ' does not exist'
        );
    END IF;
    RETURN _assets_are('privileges', grants, $3, $4);
END;
$$ LANGUAGE plpgsql;

-- Get extensions in a given schema
CREATE OR REPLACE FUNCTION _extensions( NAME )
RETURNS SETOF NAME AS $$
    SELECT e.extname
      FROM pg_catalog.pg_namespace n
      JOIN pg_catalog.pg_extension e ON n.oid = e.extnamespace
     WHERE n.nspname = $1
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _extensions()
RETURNS SETOF NAME AS $$
    SELECT extname FROM pg_catalog.pg_extension
$$ LANGUAGE SQL;

-- extensions_are( schema, extensions, description )
CREATE OR REPLACE FUNCTION extensions_are( NAME, NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'extensions',
        ARRAY(SELECT _extensions($1) EXCEPT SELECT unnest($2)),
        ARRAY(SELECT unnest($2) EXCEPT SELECT _extensions($1)),
        $3
    );
$$ LANGUAGE SQL;

-- extensions_are( schema, extensions)
CREATE OR REPLACE FUNCTION extensions_are( NAME, NAME[] )
RETURNS TEXT AS $$
  SELECT extensions_are(
        $1, $2,
        'Schema ' || quote_ident($1) || ' should have the correct extensions'
    );
$$ LANGUAGE SQL;

-- extensions_are( extensions, description )
CREATE OR REPLACE FUNCTION extensions_are( NAME[], TEXT )
RETURNS TEXT AS $$
    SELECT _are(
        'extensions',
        ARRAY(SELECT _extensions() EXCEPT SELECT unnest($1)),
        ARRAY(SELECT unnest($1) EXCEPT SELECT _extensions()),
        $2
    );
$$ LANGUAGE SQL;

-- extensions_are( schema, extensions)
CREATE OR REPLACE FUNCTION extensions_are( NAME[] )
RETURNS TEXT AS $$
  SELECT extensions_are($1, 'Should have the correct extensions');
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _error_diag(
    sqlstate TEXT DEFAULT NULL,
    errmsg   TEXT DEFAULT NULL,
    detail   TEXT DEFAULT NULL,
    hint     TEXT DEFAULT NULL,
    context  TEXT DEFAULT NULL,
    schname  TEXT DEFAULT NULL,
    tabname  TEXT DEFAULT NULL,
    colname  TEXT DEFAULT NULL,
    chkname  TEXT DEFAULT NULL,
    typname  TEXT DEFAULT NULL
) RETURNS TEXT AS $$
    SELECT COALESCE(
               COALESCE( NULLIF(sqlstate, '') || ': ', '' ) || COALESCE( NULLIF(errmsg, ''), '' ),
               'NO ERROR FOUND'
           )
        || COALESCE(E'\n        DETAIL:     ' || nullif(detail, ''), '')
        || COALESCE(E'\n        HINT:       ' || nullif(hint, ''), '')
        || COALESCE(E'\n        SCHEMA:     ' || nullif(schname, ''), '')
        || COALESCE(E'\n        TABLE:      ' || nullif(tabname, ''), '')
        || COALESCE(E'\n        COLUMN:     ' || nullif(colname, ''), '')
        || COALESCE(E'\n        CONSTRAINT: ' || nullif(chkname, ''), '')
        || COALESCE(E'\n        TYPE:       ' || nullif(typname, ''), '')
        -- We need to manually indent all the context lines
        || COALESCE(E'\n        CONTEXT:\n'
               || regexp_replace(NULLIF( context, ''), '^', '            ', 'gn'
           ), '');
$$ LANGUAGE sql IMMUTABLE;

-- lives_ok( sql, description )
CREATE OR REPLACE FUNCTION lives_ok ( TEXT, TEXT )
RETURNS TEXT AS $$
DECLARE
    code  TEXT := _query($1);
    descr ALIAS FOR $2;
    detail  text;
    hint    text;
    context text;
    schname text;
    tabname text;
    colname text;
    chkname text;
    typname text;
BEGIN
    EXECUTE code;
    RETURN ok( TRUE, descr );
EXCEPTION WHEN OTHERS THEN
    -- There should have been no exception.
    GET STACKED DIAGNOSTICS
        detail  = PG_EXCEPTION_DETAIL,
        hint    = PG_EXCEPTION_HINT,
        context = PG_EXCEPTION_CONTEXT,
        schname = SCHEMA_NAME,
        tabname = TABLE_NAME,
        colname = COLUMN_NAME,
        chkname = CONSTRAINT_NAME,
        typname = PG_DATATYPE_NAME;
    RETURN ok( FALSE, descr ) || E'\n' || diag(
           '    died: ' || _error_diag(SQLSTATE, SQLERRM, detail, hint, context, schname, tabname, colname, chkname, typname)
    );
END;
$$ LANGUAGE plpgsql;

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
        RETURN NEXT '    ' || diag_test_name('Subtest: ' || tests[i]);

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
                             CASE tfaild WHEN 1 THEN '' ELSE 's' END
                             || ' of ' || fnumb
                        );
                    END IF;
                END IF;

            EXCEPTION WHEN raise_exception THEN
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

