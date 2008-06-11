-- ## CREATE SCHEMA TAPSCHEMA;
-- ## SET search_path TO TAPSCHEMA,public;

CREATE OR REPLACE FUNCTION plan( integer ) RETURNS TEXT AS $$
BEGIN
    BEGIN
    EXECUTE '
    CREATE TEMP TABLE __tcache__ (
        label TEXT    NOT NULL,
        value integer NOT NULL
    );

    CREATE TEMP TABLE __tresults__ (
        numb   SERIAL           PRIMARY KEY,
        ok     BOOLEAN NOT NULL DEFAULT TRUE,
        aok    BOOLEAN NOT NULL DEFAULT TRUE,
        descr  TEXT    NOT NULL DEFAULT '''',
        type   TEXT    NOT NULL DEFAULT '''',
        reason TEXT    NOT NULL DEFAULT ''''
    );
    ';

    EXCEPTION WHEN duplicate_table THEN
        -- Raise an exception if there's already a plan.
        EXECUTE 'SELECT TRUE FROM __tcache__ WHERE label = ''plan''';
        IF FOUND THEN
           RAISE EXCEPTION 'You tried to plan twice!';
        END IF;
    END;

    -- Save the plan and return.
    EXECUTE 'INSERT INTO __tcache__ VALUES ( ''plan'', ' || $1 || ' )';
    RETURN '1..' || $1;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION no_plan( ) RETURNS SETOF boolean AS $$
BEGIN
    PERFORM plan(0);
    RETURN;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION _get ( text ) RETURNS integer AS $$
DECLARE
    ret integer;
BEGIN
    EXECUTE 'SELECT value FROM __tcache__ WHERE label = ' || quote_literal($1) || ' LIMIT 1' INTO ret;
    RETURN ret;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION _set ( text, integer ) RETURNS integer AS $$
BEGIN
    EXECUTE 'UPDATE __tcache__ SET value = ' || $2  || ' WHERE label = ' || quote_literal($1);
    IF NOT FOUND THEN
        EXECUTE 'INSERT INTO __tcache__ values (' || quote_literal($1) || ', ' || $2 || ')';
    END IF;
    RETURN $2;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION add_result ( boolean, bool, text, text, text )
RETURNS integer AS $$
BEGIN
    EXECUTE 'INSERT INTO __tresults__ ( ok, aok, descr, type, reason )
    VALUES( ' || $1 || ', ' || $2 || ', ' || COALESCE(quote_literal($3), '''''') || ', '
          || quote_literal($4) || ', ' || quote_literal($5) || ' )';
    RETURN lastval();
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION num_failed () RETURNS INTEGER AS $$
DECLARE
    ret integer;
BEGIN
    EXECUTE 'SELECT COUNT(*)::INTEGER FROM __tresults__ WHERE ok = FALSE' INTO ret;
    RETURN ret;
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION finish () RETURNS SETOF TEXT AS $$
DECLARE
    curr_test  integer = _get('curr_test');
    exp_tests  integer = _get('plan');
    num_failed integer = num_failed();
    plural     char    = CASE exp_tests WHEN 1 THEN 's' ELSE '' END;
BEGIN

   IF curr_test IS NULL THEN
       RAISE EXCEPTION '%', diag( 'No tests run!' );
   END IF;

   IF exp_tests = 0 THEN
        -- No plan. Output one now.
       exp_tests = curr_test;
       RETURN NEXT '1..' || exp_tests;
   END IF;

   IF curr_test < exp_tests THEN
       RETURN NEXT diag(
           'Looks like you planned ' || exp_tests || ' test' ||
           plural || ' but only ran ' || curr_test
       );
   ELSIF curr_test > exp_tests THEN
       RETURN NEXT diag(
           'Looks like you planned ' || exp_tests || ' test' ||
           plural || ' but ran ' || curr_test - exp_tests || ' extra'
       );
   ELSIF num_failed > 0 THEN
       RETURN NEXT diag(
           'Looks like you failed ' || num_failed || ' test' ||
           plural || ' of ' || exp_tests
       );
   ELSE
       
   END IF;
   RETURN;
END;
$$ LANGUAGE plpgsql;


CREATE OR REPLACE FUNCTION diag ( msg text ) RETURNS TEXT AS $$
BEGIN
    RETURN regexp_replace( msg, '^', '# ', 'gn' );
END;
$$ LANGUAGE plpgsql strict;

CREATE OR REPLACE FUNCTION ok ( ok boolean, descr text ) RETURNS TEXT AS $$
DECLARE
   test_num integer;
BEGIN
    IF _get('plan') IS NULL THEN
        RAISE EXCEPTION 'You tried to run a test without a plan! Gotta have a plan';
    END IF;

    test_num := add_result( ok, ok, descr, '', '' );

    RETURN (CASE ok WHEN TRUE THEN '' ELSE 'not ' END)
           || 'ok ' || _set( 'curr_test', test_num )
           || CASE descr WHEN '' THEN '' ELSE COALESCE( ' - ' || descr, '' ) END
           || CASE ok WHEN TRUE THEN '' ELSE E'\n' ||
                diag('Failed test ' || test_num ||
                CASE descr WHEN '' THEN '' ELSE COALESCE(': "' || descr || '"', '') END )
           END;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION ok ( boolean ) RETURNS TEXT AS $$      
    SELECT ok( $1, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION is (anyelement, anyelement, text) RETURNS TEXT AS $$
DECLARE
  result boolean := $1 = $2;
  output text    := ok( result, $3);
BEGIN
    RETURN output || CASE result WHEN TRUE THEN '' ELSE E'\n' || diag(
           '         got: ' || COALESCE( $1::text, 'NULL' ) ||
        E'\n    expected: ' || COALESCE( $2::text, 'NULL' )
    ) END;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION is (anyelement, anyelement) RETURNS TEXT AS $$
    SELECT is( $1, $2, NULL);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION isnt (anyelement, anyelement, text) RETURNS TEXT AS $$
DECLARE
  result boolean := $1 <> $2;
  output text    := ok( result, $3 );
BEGIN
    RETURN output || CASE result WHEN TRUE THEN '' ELSE E'\n' || diag(
           '    ' || COALESCE( $1::text, 'NULL' ) ||
        E'\n      <>' ||
        E'\n    ' || COALESCE( $2::text, 'NULL' )
    ) END;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION isnt (anyelement, anyelement) RETURNS TEXT AS $$
    SELECT isnt( $1, $2, NULL);
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _alike (
   result boolean,
   got    anyelement,
   rx     text,
   descr  text
) RETURNS TEXT AS $$
DECLARE
  output text    := ok( result, descr);
BEGIN
    RETURN output || CASE result WHEN TRUE THEN '' ELSE E'\n' || diag(
           '                  ' || COALESCE( quote_literal(got), 'NULL' ) ||
        E'\n   doesn''t match: ' || COALESCE( quote_literal(rx), 'NULL' )
    ) END;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION matches ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~ $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION matches ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~ $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION imatches ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~* $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION imatches ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~* $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION alike ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~~ $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION alike ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~~ $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION ialike ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~~* $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION ialike ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _alike( $1 ~~* $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _unalike (
   result boolean,
   got    anyelement,
   rx     text,
   descr  text
) RETURNS TEXT AS $$
DECLARE
  output text    := ok( result, descr);
BEGIN
    RETURN output || CASE result WHEN TRUE THEN '' ELSE E'\n' || diag(
           '                  ' || COALESCE( quote_literal(got), 'NULL' ) ||
        E'\n         matches: ' || COALESCE( quote_literal(rx), 'NULL' )
    ) END;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION doesnt_match ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~ $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION doesnt_match ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~ $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION doesnt_imatch ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~* $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION doesnt_imatch ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~* $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION unalike ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~~ $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION unalike ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~~ $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION unialike ( anyelement, text, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~~* $2, $1, $2, $3 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION unialike ( anyelement, text ) RETURNS TEXT AS $$
    SELECT _unalike( $1 !~~* $2, $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION pass ( text ) RETURNS TEXT AS $$
    SELECT ok( TRUE, $1 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION pass ( ) RETURNS TEXT AS $$
    SELECT ok( TRUE, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION fail ( text ) RETURNS TEXT AS $$
    SELECT ok( FALSE, $1 );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION fail ( ) RETURNS TEXT AS $$
    SELECT ok( FALSE, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION throws_ok (
    code   TEXT,
    err    CHAR(5),
    msg    TEXT
) RETURNS TEXT AS $$
DECLARE
    descr TEXT := COALESCE( msg, 'threw ' || COALESCE( err, 'an exception' )  );
BEGIN
    EXECUTE code;
    RETURN ok( FALSE, descr ) || E'\n' || diag(
           '      caught: no exception' ||
        E'\n    expected: ' || COALESCE( err, 'an exception' )
    );
EXCEPTION WHEN OTHERS THEN
    IF err IS NULL OR SQLSTATE = err THEN
        -- The expected error was thrown.
        RETURN ok( TRUE, descr );
    ELSE
        -- This was not the expected error.
        RETURN ok( FALSE, descr ) || E'\n' || diag(
               '      caught: ' || SQLSTATE || ': ' || SQLERRM ||
            E'\n    expected: ' || COALESCE( err, 'an exception')
        );
    END IF;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION throws_ok ( TEXT, CHAR(5) ) RETURNS TEXT AS $$
    SELECT throws_ok( $1, $2, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION throws_ok ( TEXT ) RETURNS TEXT AS $$
    SELECT throws_ok( $1, NULL, NULL );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION lives_ok (
    code   TEXT,
    descr  TEXT
) RETURNS TEXT AS $$
BEGIN
    EXECUTE code;
    RETURN ok( TRUE, descr );
EXCEPTION WHEN OTHERS THEN
    -- There should have been no exception.
    RETURN ok( FALSE, descr ) || E'\n' || diag(
           '        died: ' || SQLSTATE || ': ' || SQLERRM
    );
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION lives_ok ( TEXT ) RETURNS TEXT AS $$
    SELECT lives_ok( $1, NULL );
$$ LANGUAGE SQL;
