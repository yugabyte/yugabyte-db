\unset ECHO
\i test/setup.sql

SELECT plan(1009);
-- SELECT * FROM no_plan();

CREATE SCHEMA someschema;
CREATE FUNCTION someschema.huh () RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;
CREATE FUNCTION someschema.bah (int, text) RETURNS BOOL
AS 'BEGIN RETURN TRUE; END;' LANGUAGE plpgsql;

CREATE FUNCTION public.yay () RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL STRICT SECURITY DEFINER VOLATILE;
CREATE FUNCTION public.oww (int, text) RETURNS BOOL
AS 'BEGIN RETURN TRUE; END;' LANGUAGE plpgsql IMMUTABLE;
CREATE FUNCTION public.pet () RETURNS SETOF BOOL
AS 'BEGIN RETURN NEXT TRUE; RETURN; END;' LANGUAGE plpgsql STABLE;

DO $$
BEGIN
    IF pg_version_num() >= 140000 THEN
        CREATE AGGREGATE public.tap_accum (
            sfunc    = array_append,
            basetype = anycompatible,
            stype    = anycompatiblearray,
            initcond = '{}'
        );
        CREATE FUNCTION atype()
        RETURNS text AS 'SELECT ''anycompatiblearray''::text'
        LANGUAGE SQL IMMUTABLE;
        CREATE FUNCTION etype()
        RETURNS text AS 'SELECT ''anycompatible''::text'
        LANGUAGE SQL IMMUTABLE;
    ELSE
        CREATE AGGREGATE public.tap_accum (
            sfunc    = array_append,
            basetype = anyelement,
            stype    = anyarray,
            initcond = '{}'
        );
        CREATE FUNCTION atype()
        RETURNS text AS 'SELECT ''anyarray''::text'
        LANGUAGE SQL IMMUTABLE;
        CREATE FUNCTION etype()
        RETURNS text AS 'SELECT ''anyelement''::text'
        LANGUAGE SQL IMMUTABLE;
    END IF;
END;
$$;

/****************************************************************************/
-- Test has_function().
SELECT * FROM check_test(
    has_function( 'now' ),
    true,
    'simple function',
    'Function now() should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'pg_catalog', 'now'::name ),
    true,
    'simple schema.function',
    'Function pg_catalog.now() should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'now', 'whatever' ),
    true,
    'simple function desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_function( 'now', '{}'::name[] ),
    true,
    'simple with 0 args',
    'Function now() should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'now', '{}'::name[], 'whatever' ),
    true,
    'simple with 0 args desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_function( 'pg_catalog', 'now', '{}'::name[] ),
    true,
    'simple schema.func with 0 args',
    'Function pg_catalog.now() should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'pg_catalog', 'now', 'whatever' ),
    true,
    'simple schema.func with desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_function( 'pg_catalog', 'now', '{}'::name[], 'whatever' ),
    true,
    'simple schema.func with 0 args, desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_function( 'lower', '{text}'::name[] ),
    true,
    'simple function with 1 arg',
    'Function lower(text) should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'decode', '{text,text}'::name[] ),
    true,
    'simple function with 2 args',
    'Function decode(text, text) should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'array_cat', ARRAY[atype(), atype()] ),
    true,
    'simple array function',
    'Function array_cat(' || atype() || ', ' || atype() || ') should exist',
    ''
);

-- Check a custom function with an array argument.
CREATE FUNCTION __cat__ (text[]) RETURNS BOOLEAN
AS 'SELECT TRUE'
LANGUAGE SQL;

SELECT * FROM check_test(
    has_function( '__cat__', '{text[]}'::name[] ),
    true,
    'custom array function',
    'Function __cat__(text[]) should exist',
    ''
);

-- Check a custom function with a numeric argument.
CREATE FUNCTION __cat__ (numeric(10,2)) RETURNS BOOLEAN
AS 'SELECT TRUE'
LANGUAGE SQL;

SELECT * FROM check_test(
    has_function( '__cat__', '{numeric}'::name[] ),
    true,
    'custom numeric function',
    'Function __cat__(numeric) should exist',
    ''
);

-- Check a custom function with a schema-qualified arugment.
CREATE DOMAIN public.intword AS TEXT CHECK (VALUE IN ('one', 'two', 'three'));
CREATE FUNCTION __cat__(intword) RETURNS BOOLEAN AS 'SELECT TRUE' LANGUAGE SQL;
SELECT * FROM check_test(
    has_function( '__cat__', '{intword}'::name[] ),
    true,
    'custom unqualified function with intword unqualified argument',
    'Function __cat__(intword) should exist',
    ''
);

SELECT * FROM check_test(
    has_function( '__cat__', '{public.intword}'::name[] ),
    true,
    'custom unqualified function with intword qualified argument',
    'Function __cat__(public.intword) should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'public', '__cat__', '{intword}'::name[] ),
    true,
    'custom qualified function with intword unqualified argument',
    'Function public.__cat__(intword) should exist',
    ''
);

SELECT * FROM check_test(
    has_function( 'public', '__cat__', '{public.intword}'::text[] ),
    true,
    'custom qualified function with intword qualified argument',
    'Function public.__cat__(public.intword) should exist',
    ''
);

-- Check failure output.
SELECT * FROM check_test(
    has_function( '__cat__', '{varchar[]}'::name[] ),
    false,
    'failure output',
    'Function __cat__(varchar[]) should exist',
    '' -- No diagnostics.
);

/****************************************************************************/
-- Test hasnt_function().
SELECT * FROM check_test(
    hasnt_function( 'now' ),
    false,
    'simple function',
    'Function now() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'pg_catalog', 'now'::name ),
    false,
    'simple schema.function',
    'Function pg_catalog.now() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'now', 'whatever' ),
    false,
    'simple function desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'now', '{}'::name[] ),
    false,
    'simple with 0 args',
    'Function now() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'now', '{}'::name[], 'whatever' ),
    false,
    'simple with 0 args desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'pg_catalog', 'now', '{}'::name[] ),
    false,
    'simple schema.func with 0 args',
    'Function pg_catalog.now() should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'pg_catalog', 'now', 'whatever' ),
    false,
    'simple schema.func with desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'pg_catalog', 'now', '{}'::name[], 'whatever' ),
    false,
    'simple schema.func with 0 args, desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'lower', '{text}'::name[] ),
    false,
    'simple function with 1 arg',
    'Function lower(text) should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'decode', '{text,text}'::name[] ),
    false,
    'simple function with 2 args',
    'Function decode(text, text) should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( 'array_cat', ARRAY[atype(), atype()] ),
    false,
    'simple array function',
    'Function array_cat(' || atype() || ', ' || atype() || ') should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( '__cat__', '{text[]}'::name[] ),
    false,
    'custom array function',
    'Function __cat__(text[]) should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_function( '__cat__', '{numeric}'::name[] ),
    false,
    'custom numeric function',
    'Function __cat__(numeric) should not exist',
    ''
);

/****************************************************************************/
-- Try can() function names.
SELECT * FROM check_test(
    can( 'pg_catalog', ARRAY['lower', 'upper'], 'whatever' ),
    true,
    'can(schema) with desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    can( 'pg_catalog', ARRAY['lower', 'upper'] ),
    true,
    'can(schema)',
    'Schema pg_catalog can',
    ''
);

SELECT * FROM check_test(
    can( 'pg_catalog', ARRAY['lower', 'foo', 'bar'], 'whatever' ),
    false,
    'fail can(schema) with desc',
    'whatever',
    '    pg_catalog.foo() missing
    pg_catalog.bar() missing'
);

SELECT * FROM check_test(
    can( 'someschema', ARRAY['huh', 'lower', 'upper'], 'whatever' ),
    false,
    'fail can(someschema) with desc',
    'whatever',
    '    someschema.lower() missing
    someschema.upper() missing'
);

SELECT * FROM check_test(
    can( ARRAY['__cat__', 'lower', 'upper'], 'whatever' ),
    true,
    'can() with desc',
    'whatever',
    ''
);

SELECT * FROM check_test(
    can( ARRAY['lower', 'upper'] ),
    true,
    'can(schema)',
    'Schema ' || array_to_string(current_schemas(true), ' or ') || ' can',
    ''
);

SELECT * FROM check_test(
    can( ARRAY['__cat__', 'foo', 'bar'], 'whatever' ),
    false,
    'fail can() with desc',
    'whatever',
    '    foo() missing
    bar() missing'
);

/****************************************************************************/
-- Test function_lang_is().
SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', '{}'::name[], 'sql', 'whatever' ),
    true,
    'function_lang_is(schema, func, 0 args, sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', '{}'::name[], 'sql' ),
    true,
    'function_lang_is(schema, func, 0 args, sql)',
    'Function someschema.huh() should be written in sql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'bah', '{"integer", "text"}'::name[], 'plpgsql', 'whatever' ),
    true,
    'function_lang_is(schema, func, args, plpgsql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'bah', '{"integer", "text"}'::name[], 'plpgsql' ),
    true,
    'function_lang_is(schema, func, args, plpgsql)',
    'Function someschema.bah(integer, text) should be written in plpgsql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', '{}'::name[], 'perl', 'whatever' ),
    false,
    'function_lang_is(schema, func, 0 args, perl, desc)',
    'whatever',
    '        have: sql
        want: perl'
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'why', '{}'::name[], 'sql', 'whatever' ),
    false,
    'function_lang_is(schema, non-func, 0 args, sql, desc)',
    'whatever',
    '    Function someschema.why() does not exist'
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'why', '{"integer", "text"}'::name[], 'plpgsql' ),
    false,
    'function_lang_is(schema, func, args, plpgsql)',
    'Function someschema.why(integer, text) should be written in plpgsql',
    '    Function someschema.why(integer, text) does not exist'
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', 'sql', 'whatever' ),
    true,
    'function_lang_is(schema, func, sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', 'sql'::name ),
    true,
    'function_lang_is(schema, func, sql)',
    'Function someschema.huh() should be written in sql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'huh', 'perl', 'whatever' ),
    false,
    'function_lang_is(schema, func, perl, desc)',
    'whatever',
    '        have: sql
        want: perl'
);

SELECT * FROM check_test(
    function_lang_is( 'someschema', 'why', 'sql', 'whatever' ),
    false,
    'function_lang_is(schema, non-func, sql, desc)',
    'whatever',
    '    Function someschema.why() does not exist'
);

SELECT * FROM check_test(
    function_lang_is( 'yay', '{}'::name[], 'sql', 'whatever' ),
    true,
    'function_lang_is(func, 0 args, sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'yay', '{}'::name[], 'sql' ),
    true,
    'function_lang_is(func, 0 args, sql)',
    'Function yay() should be written in sql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'oww', '{"integer", "text"}'::name[], 'plpgsql', 'whatever' ),
    true,
    'function_lang_is(func, args, plpgsql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'oww', '{"integer", "text"}'::name[], 'plpgsql' ),
    true,
    'function_lang_is(func, args, plpgsql)',
    'Function oww(integer, text) should be written in plpgsql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'yay', '{}'::name[], 'perl', 'whatever' ),
    false,
    'function_lang_is(func, 0 args, perl, desc)',
    'whatever',
    '        have: sql
        want: perl'
);

SELECT * FROM check_test(
    function_lang_is( 'why', '{}'::name[], 'sql', 'whatever' ),
    false,
    'function_lang_is(non-func, 0 args, sql, desc)',
    'whatever',
    '    Function why() does not exist'
);

SELECT * FROM check_test(
    function_lang_is( 'why', '{"integer", "text"}'::name[], 'plpgsql' ),
    false,
    'function_lang_is(func, args, plpgsql)',
    'Function why(integer, text) should be written in plpgsql',
    '    Function why(integer, text) does not exist'
);

SELECT * FROM check_test(
    function_lang_is( 'yay', 'sql', 'whatever' ),
    true,
    'function_lang_is(func, sql, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'yay', 'sql' ),
    true,
    'function_lang_is(func, sql)',
    'Function yay() should be written in sql',
    ''
);

SELECT * FROM check_test(
    function_lang_is( 'yay', 'perl', 'whatever' ),
    false,
    'function_lang_is(func, perl, desc)',
    'whatever',
    '        have: sql
        want: perl'
);

SELECT * FROM check_test(
    function_lang_is( 'why', 'sql', 'whatever' ),
    false,
    'function_lang_is(non-func, sql, desc)',
    'whatever',
    '    Function why() does not exist'
);

/****************************************************************************/
-- Test function_returns().
SELECT * FROM check_test(
    function_returns( 'someschema', 'huh', '{}'::name[], 'boolean', 'whatever' ),
    true,
    'function_returns(schema, func, 0 args, bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'huh', '{}'::name[], 'boolean' ),
    true,
    'function_returns(schema, func, 0 args, bool)',
    'Function someschema.huh() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'bah', ARRAY['integer', 'text'], 'boolean', 'whatever' ),
    true,
    'function_returns(schema, func, args, bool, false)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'bah', ARRAY['integer', 'text'], 'boolean' ),
    true,
    'function_returns(schema, func, args, bool)',
    'Function someschema.bah(integer, text) should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'public', 'pet', '{}'::name[], 'setof boolean', 'whatever' ),
    true,
    'function_returns(schema, func, 0 args, setof bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'public', 'pet', '{}'::name[], 'setof boolean' ),
    true,
    'function_returns(schema, func, 0 args, setof bool)',
    'Function public.pet() should return setof boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'huh', 'boolean', 'whatever' ),
    true,
    'function_returns(schema, func, bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'huh'::name, 'boolean' ),
    true,
    'function_returns(schema, func, bool)',
    'Function someschema.huh() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'bah', 'boolean', 'whatever' ),
    true,
    'function_returns(schema, other func, bool, false)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'someschema', 'bah'::name, 'boolean' ),
    true,
    'function_returns(schema, other func, bool)',
    'Function someschema.bah() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'public', 'pet', 'setof boolean', 'whatever' ),
    true,
    'function_returns(schema, func, setof bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'public', 'pet'::name, 'setof boolean' ),
    true,
    'function_returns(schema, func, setof bool)',
    'Function public.pet() should return setof boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'yay', '{}'::name[], 'boolean', 'whatever' ),
    true,
    'function_returns(func, 0 args, bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'yay', '{}'::name[], 'boolean' ),
    true,
    'function_returns(func, 0 args, bool)',
    'Function yay() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'oww', ARRAY['integer', 'text'], 'boolean', 'whatever' ),
    true,
    'function_returns(func, args, bool, false)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'oww', ARRAY['integer', 'text'], 'boolean' ),
    true,
    'function_returns(func, args, bool)',
    'Function oww(integer, text) should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'pet', '{}'::name[], 'setof boolean', 'whatever' ),
    true,
    'function_returns(func, 0 args, setof bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'pet', '{}'::name[], 'setof boolean' ),
    true,
    'function_returns(func, 0 args, setof bool)',
    'Function pet() should return setof boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'yay', 'boolean', 'whatever' ),
    true,
    'function_returns(func, bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'yay', 'boolean' ),
    true,
    'function_returns(func, bool)',
    'Function yay() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'oww', 'boolean', 'whatever' ),
    true,
    'function_returns(other func, bool, false)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'oww', 'boolean' ),
    true,
    'function_returns(other func, bool)',
    'Function oww() should return boolean',
    ''
);

SELECT * FROM check_test(
    function_returns( 'pet', 'setof boolean', 'whatever' ),
    true,
    'function_returns(func, setof bool, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    function_returns( 'pet', 'setof boolean' ),
    true,
    'function_returns(func, setof bool)',
    'Function pet() should return setof boolean',
    ''
);

/****************************************************************************/
-- Test is_definer() isnt_definer().
SELECT * FROM check_test(
    is_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_definer(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay', '{}'::name[] ),
    true,
    'is_definer(schema, func, 0 args)',
    'Function public.yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', '{}'::name[] ),
    false,
    'isnt_definer(schema, func, 0 args)',
    'Function public.yay() should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_definer(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_definer(schema, func, args)',
    'Function public.oww(integer, text) should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_definer(schema, func, args)',
    'Function public.oww(integer, text) should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay', 'whatever' ),
    true,
    'is_definer(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', 'whatever' ),
    false,
    'isnt_definer(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay'::name ),
    true,
    'is_definer(schema, func)',
    'Function public.yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay'::name ),
    false,
    'isnt_definer(schema, func)',
    'Function public.yay() should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_definer(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay', '{}'::name[] ),
    true,
    'is_definer(schema, func, 0 args)',
    'Function public.yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', '{}'::name[] ),
    false,
    'isnt_definer(schema, func, 0 args)',
    'Function public.yay() should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_definer(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_definer(schema, func, args)',
    'Function public.oww(integer, text) should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_definer(schema, func, args)',
    'Function public.oww(integer, text) should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay', 'whatever' ),
    true,
    'is_definer(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay', 'whatever' ),
    false,
    'isnt_definer(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'public', 'yay'::name ),
    true,
    'is_definer(schema, func)',
    'Function public.yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'public', 'yay'::name ),
    false,
    'isnt_definer(schema, func)',
    'Function public.yay() should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_definer(func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'yay', '{}'::name[] ),
    true,
    'is_definer(func, 0 args)',
    'Function yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'yay', '{}'::name[] ),
    false,
    'isnt_definer(func, 0 args)',
    'Function yay() should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_definer(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_definer(func, args)',
    'Function oww(integer, text) should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_definer(func, args)',
    'Function oww(integer, text) should not be security definer',
    ''
);

SELECT * FROM check_test(
    is_definer( 'yay', 'whatever' ),
    true,
    'is_definer(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'yay', 'whatever' ),
    false,
    'isnt_definer(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_definer( 'yay'::name ),
    true,
    'is_definer(func)',
    'Function yay() should be security definer',
    ''
);

SELECT * FROM check_test(
    isnt_definer( 'yay'::name ),
    false,
    'isnt_definer(func)',
    'Function yay() should not be security definer',
    ''
);

/****************************************************************************/
-- Test is_normal_function() and isnt_normal_function().

-- is_normal_function ( NAME, NAME, NAME[], TEXT )
-- isnt_normal_function ( NAME, NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'huh', '{}', 'whatever' ),
    true,
    'is_normal_function(schema, func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    false,
    'is_normal_function(schema, agg, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'huh', '{}', 'whatever' ),
    false,
    'isnt_normal_function(schema, func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    true,
    'isnt_normal_function(schema, agg, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'someschema', 'bah', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'is_normal_function(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    false,
    'is_normal_function(schema, agg, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'bah', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_normal_function(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    true,
    'isnt_normal_function(schema, agg, args, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'nonesuch', '{}', 'whatever' ),
    false,
    'is_normal_function(schema, nofunc, noargs, desc)',
    'whatever',
    '    Function someschema.nonesuch() does not exist'
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'nonesuch', ARRAY[etype()], 'whatever' ),
    false,
    'isnt_normal_function(schema, noagg, args, desc)',
    'whatever',
    '    Function someschema.nonesuch(' || etype() || ') does not exist'
);

-- is_normal_function( NAME, NAME, NAME[] )
-- isnt_normal_function( NAME, NAME, NAME[] )
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'huh', '{}'::name[] ),
    true,
    'is_normal_function(schema, func, noargs)',
    'Function someschema.huh() should be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'tap_accum', ARRAY[etype()] ),
    false,
    'is_normal_function(schema, agg, noargs)',
    'Function public.tap_accum(' || etype() || ') should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'huh', '{}'::name[] ),
    false,
    'isnt_normal_function(schema, func, noargs)',
    'Function someschema.huh() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'public', 'tap_accum', ARRAY[etype()] ),
    true,
    'isnt_normal_function(schema, agg, noargs)',
    'Function public.tap_accum(' || etype() || ') should not be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'someschema', 'bah', ARRAY['integer', 'text'] ),
    true,
    'is_normal_function(schema, func2, args)',
    'Function someschema.bah(integer, text) should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'bah', ARRAY['integer', 'text'] ),
    false,
    'isnt_normal_function(schema, func2, args)',
    'Function someschema.bah(integer, text) should not be a normal function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'nonesuch', '{}'::name[] ),
    false,
    'is_normal_function(schema, func, noargs)',
    'Function someschema.nonesuch() should be a normal function',
    '    Function someschema.nonesuch() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'nonesuch', ARRAY[etype()] ),
    false,
    'is_normal_function(schema, nofunc, noargs)',
    'Function public.nonesuch(' || etype() || ') should be a normal function',
    '    Function public.nonesuch(' || etype() || ') does not exist'
    ''
);

-- is_normal_function ( NAME, NAME, TEXT )
-- isnt_normal_function ( NAME, NAME, TEXT )
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'huh', 'whatever' ),
    true,
    'is_normal_function(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'tap_accum', 'whatever' ),
    false,
    'is_normal_function(schema, agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'huh', 'whatever' ),
    false,
    'isnt_normal_function(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'public', 'tap_accum', 'whatever' ),
    true,
    'isnt_normal_function(schema, agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'someschema', 'bah', 'whatever' ),
    true,
    'is_normal_function(schema, func2, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'bah', 'whatever' ),
    false,
    'isnt_normal_function(schema, func2, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'nonesuch', 'whatever' ),
    false,
    'is_normal_function(schema, nofunc, desc)',
    'whatever',
    '    Function someschema.nonesuch() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'nonesuch', 'whatever' ),
    false,
    'is_normal_function(schema, noagg, desc)',
    'whatever',
    '    Function public.nonesuch() does not exist'
);

-- is_normal_function( NAME, NAME )
-- isnt_normal_function( NAME, NAME )
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'huh'::name ),
    true,
    'is_normal_function(schema, func)',
    'Function someschema.huh() should be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'tap_accum'::name ),
    false,
    'is_normal_function(schema, agg)',
    'Function public.tap_accum() should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'huh'::name ),
    false,
    'isnt_normal_function(schema, func)',
    'Function someschema.huh() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'public', 'tap_accum'::name ),
    true,
    'isnt_normal_function(schema, agg)',
    'Function public.tap_accum() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'someschema', 'bah'::name ),
    true,
    'is_normal_function(schema, func2, args)',
    'Function someschema.bah() should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'someschema', 'bah'::name ),
    false,
    'isnt_normal_function(schema, func2)',
    'Function someschema.bah() should not be a normal function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'someschema', 'nonesuch'::name ),
    false,
    'is_normal_function(schema, nofunc)',
    'Function someschema.nonesuch() should be a normal function',
    '    Function someschema.nonesuch() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'public', 'nonesuch'::name ),
    false,
    'is_normal_function(schema, nogg)',
    'Function public.nonesuch() should be a normal function',
    '    Function public.nonesuch() does not exist'
);

-- is_normal_function ( NAME, NAME[], TEXT )
-- isnt_normal_function ( NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_normal_function( 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_normal_function(func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'tap_accum', ARRAY[etype()], 'whatever' ),
    false,
    'is_normal_function(func, agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_normal_function(func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'tap_accum', ARRAY[etype()], 'whatever' ),
    true,
    'isnt_normal_function(func, agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'is_normal_function(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'isnt_normal_function(func, args, desc)',
    'whatever',
    ''
);

-- test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'nonesuch', '{}'::name[], 'whatever' ),
    false,
    'is_normal_function(nofunc, noargs, desc)',
    'whatever',
    '    Function nonesuch() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'nonesuch', ARRAY[etype()], 'whatever' ),
    false,
    'is_normal_function(func, noagg, desc)',
    'whatever',
    '    Function nonesuch(' || etype() || ') does not exist'
);

-- is_normal_function( NAME, NAME[] )
-- isnt_normal_function( NAME, NAME[] )
SELECT * FROM check_test(
    is_normal_function( 'yay', '{}'::name[] ),
    true,
    'is_normal_function(func, noargs)',
    'Function yay() should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'yay', '{}'::name[] ),
    false,
    'isnt_normal_function(func, noargs)',
    'Function yay() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'oww', ARRAY['integer', 'text'] ),
    true,
    'is_normal_function(func, noargs)',
    'Function oww(integer, text) should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'oww', ARRAY['integer', 'text'] ),
    false,
    'isnt_normal_function(func, noargs)',
    'Function oww(integer, text) should not be a normal function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'nope', '{}'::name[] ),
    false,
    'is_normal_function(nofunc, noargs)',
    'Function nope() should be a normal function',
    '    Function nope() does not exist'
);

SELECT * FROM check_test(
    isnt_normal_function( 'nope', '{}'::name[] ),
    false,
    'isnt_normal_function(fnounc, noargs)',
    'Function nope() should not be a normal function',
    '    Function nope() does not exist'
);

-- is_normal_function( NAME, TEXT )
-- isnt_normal_function( NAME, TEXT )
SELECT * FROM check_test(
    is_normal_function( 'yay', 'whatever' ),
    true,
    'is_normal_function(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'tap_accum', 'whatever' ),
    false,
    'is_normal_function(agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'yay', 'howdy' ),
    false,
    'isnt_normal_function(func, desc)',
    'howdy',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'tap_accum', 'whatever' ),
    true,
    'isnt_normal_function(agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'oww', 'coyote' ),
    true,
    'is_normal_function(func2, desc)',
    'coyote',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'oww', 'hi there' ),
    false,
    'isnt_normal_function(func2, desc)',
    'hi there',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'none', 'whatever' ),
    false,
    'is_normal_function(nofunc, desc)',
    'whatever',
    '    Function "none"() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'none', 'whatever' ),
    false,
    'is_normal_function(noagg, desc)',
    'whatever',
    '    Function "none"() does not exist'
);

-- is_normal_function( NAME )
-- isnt_normal_function( NAME )
SELECT * FROM check_test(
    is_normal_function( 'yay' ),
    true,
    'is_normal_function(func)',
    'Function yay() should be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'tap_accum' ),
    false,
    'is_normal_function(agg)',
    'Function tap_accum() should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'yay' ),
    false,
    'isnt_normal_function(func)',
    'Function yay() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'tap_accum' ),
    true,
    'isnt_normal_function(agg)',
    'Function tap_accum() should not be a normal function',
    ''
);

SELECT * FROM check_test(
    is_normal_function( 'oww' ),
    true,
    'is_normal_function(func2)',
    'Function oww() should be a normal function',
    ''
);

SELECT * FROM check_test(
    isnt_normal_function( 'oww' ),
    false,
    'isnt_normal_function(func2,)',
    'Function oww() should not be a normal function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_normal_function( 'zippo' ),
    false,
    'is_normal_function(nofunc)',
    'Function zippo() should be a normal function',
    '    Function zippo() does not exist'
);

SELECT * FROM check_test(
    is_normal_function( 'zippo' ),
    false,
    'is_normal_function(noagg)',
    'Function zippo() should be a normal function',
    '    Function zippo() does not exist'
);

/****************************************************************************/
-- Test is_aggregate() and isnt_aggregate().

-- is_aggregate ( NAME, NAME, NAME[], TEXT )
-- isnt_aggregate ( NAME, NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    true,
    'is_aggregate(schema, func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'tap_accum', ARRAY[etype()], 'whatever' ),
    false,
    'isnt_aggregate(schema, agg, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_aggregate(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_aggregate(schema, func, args, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'public', 'nope', ARRAY[etype()], 'whatever' ),
    false,
    'is_aggregate(schema, nofunc, arg, desc)',
    'whatever',
    '    Function public.nope(' || etype() || ') does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'nope', ARRAY[etype()], 'whatever' ),
    false,
    'isnt_aggregate(schema, noagg, arg, desc)',
    'whatever',
    '    Function public.nope(' || etype() || ') does not exist'
);

-- is_aggregate( NAME, NAME, NAME[] )
-- isnt_aggregate( NAME, NAME, NAME[] )
SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY[etype()] ),
    true,
    'is_aggregate(schema, agg, arg)',
    'Function public.tap_accum(' || etype() || ') should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'tap_accum', ARRAY[etype()] ),
    false,
    'isnt_aggregate(schema, agg, arg)',
    'Function public.tap_accum(' || etype() || ') should not be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_aggregate(schema, func, args)',
    'Function public.oww(integer, text) should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_aggregate(schema, func, args)',
    'Function public.oww(integer, text) should not be an aggregate function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'public', 'uhuh', ARRAY[etype()] ),
    false,
    'is_aggregate(schema, noagg, arg)',
    'Function public.uhuh(' || etype() || ') should be an aggregate function',
    '    Function public.uhuh(' || etype() || ') does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'uhuh', ARRAY[etype()] ),
    false,
    'isnt_aggregate(schema, noagg, arg)',
    'Function public.uhuh(' || etype() || ') should not be an aggregate function',
    '    Function public.uhuh(' || etype() || ') does not exist'
);

-- is_aggregate ( NAME, NAME, TEXT )
-- isnt_aggregate ( NAME, NAME, TEXT )
SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', 'whatever' ),
    true,
    'is_aggregate(schema, agg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'tap_accum', 'whatever' ),
    false,
    'isnt_aggregate(schema, agg, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'public', 'nada', 'whatever' ),
    false,
    'is_aggregate(schema, noagg, desc)',
    'whatever',
    '    Function public.nada() does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'nada', 'whatever' ),
    false,
    'isnt_aggregate(schema, noagg, desc)',
    'whatever',
    '    Function public.nada() does not exist'
);

-- is_aggregate( NAME, NAME )
-- isnt_aggregate( NAME, NAME )
SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum'::name ),
    true,
    'is_aggregate(schema, agg)',
    'Function public.tap_accum() should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'tap_accum'::name ),
    false,
    'isnt_aggregate(schema, agg)',
    'Function public.tap_accum() should not be an aggregate function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'public', 'nope'::name ),
    false,
    'is_aggregate(schema, noagg)',
    'Function public.nope() should be an aggregate function',
    '    Function public.nope() does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'public', 'nope'::name ),
    false,
    'isnt_aggregate(schema, noagg)',
    'Function public.nope() should not be an aggregate function',
    '    Function public.nope() does not exist'
);

-- is_aggregate ( NAME, NAME[], TEXT )
-- isnt_aggregate ( NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_aggregate( 'tap_accum', ARRAY[etype()], 'whatever' ),
    true,
    'is_aggregate(agg, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'tap_accum', ARRAY[etype()], 'whatever' ),
    false,
    'isnt_aggregate(agg, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_aggregate(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_aggregate(func, args, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'nonesuch', ARRAY[etype()], 'whatever' ),
    false,
    'is_aggregate(noagg, arg, desc)',
    'whatever',
    '    Function nonesuch(' || etype() || ') does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'nonesuch', ARRAY[etype()], 'whatever' ),
    false,
    'isnt_aggregate(noagg, arg, desc)',
    'whatever',
    '    Function nonesuch(' || etype() || ') does not exist'
);

-- is_aggregate( NAME, NAME[] )
-- isnt_aggregate( NAME, NAME[] )
SELECT * FROM check_test(
    is_aggregate( 'tap_accum', ARRAY[etype()] ),
    true,
    'is_aggregate(agg, arg)',
    'Function tap_accum(' || etype() || ') should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'tap_accum', ARRAY[etype()] ),
    false,
    'isnt_aggregate(agg, arg)',
    'Function tap_accum(' || etype() || ') should not be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_aggregate(func, args)',
    'Function oww(integer, text) should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_aggregate(func, args)',
    'Function oww(integer, text) should not be an aggregate function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( '_zip', ARRAY[etype()] ),
    false,
    'is_aggregate(noagg, arg)',
    'Function _zip(' || etype() || ') should be an aggregate function',
    '    Function _zip(' || etype() || ') does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( '_zip', ARRAY[etype()] ),
    false,
    'isnt_aggregate(noagg, arg)',
    'Function _zip(' || etype() || ') should not be an aggregate function',
    '    Function _zip(' || etype() || ') does not exist'
);

-- is_aggregate( NAME, TEXT )
-- isnt_aggregate( NAME, TEXT )
SELECT * FROM check_test(
    is_aggregate( 'tap_accum', 'whatever' ),
    true,
    'is_aggregate(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'tap_accum', 'whatever' ),
    false,
    'isnt_aggregate(agg, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'nope', 'whatever' ),
    false,
    'is_aggregate(nofunc, desc)',
    'whatever',
    '    Function nope() does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'nope', 'whatever' ),
    false,
    'isnt_aggregate(noagg, desc)',
    'whatever',
    '    Function nope() does not exist'
);

-- is_aggregate( NAME )
-- isnt_aggregate( NAME )
SELECT * FROM check_test(
    is_aggregate( 'tap_accum'::name ),
    true,
    'is_aggregate(agg)',
    'Function tap_accum() should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    isnt_aggregate( 'tap_accum'::name ),
    false,
    'isnt_aggregate(agg)',
    'Function tap_accum() should not be an aggregate function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_aggregate( 'nope'::name ),
    false,
    'is_aggregate(noagg)',
    'Function nope() should be an aggregate function',
    '    Function nope() does not exist'
);

SELECT * FROM check_test(
    isnt_aggregate( 'nope'::name ),
    false,
    'isnt_aggregate(noagg)',
    'Function nope() should not be an aggregate function',
    '    Function nope() does not exist'
);

/****************************************************************************/
-- Test is_window() and isnt_window().

-- is_window ( NAME, NAME, NAME[], TEXT )
-- isnt_window ( NAME, NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_window( 'pg_catalog', 'ntile', ARRAY['integer'], 'whatever' ),
    true,
    'is_window(schema, win, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'ntile', ARRAY['integer'], 'whatever' ),
    false,
    'isnt_window(schema, win, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'bah', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_window(schema, func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'bah', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_window(schema, func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'pg_catalog', 'dense_rank', '{}'::name[], 'whatever' ),
    true,
    'is_window(schema, win, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'dense_rank', '{}'::name[], 'whatever' ),
    false,
    'isnt_window(schema, win, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'huh', '{}'::name[], 'whatever' ),
    false,
    'is_window(schema, func, noarg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'dense_rank', '{}'::name[], 'whatever' ),
    false,
    'is_window(schema, win, noargs, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'someschema', 'nope', ARRAY['integer'], 'whatever' ),
    false,
    'is_window(schema, nowin, arg, desc)',
    'whatever',
    '    Function someschema.nope(integer) does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'nope', ARRAY['integer'], 'whatever' ),
    false,
    'isnt_window(schema, nowin, arg, desc)',
    'whatever',
    '    Function someschema.nope(integer) does not exist'
);

-- is_window ( NAME, NAME, NAME[] )
-- isnt_window ( NAME, NAME, NAME[] )
SELECT * FROM check_test(
    is_window( 'pg_catalog', 'ntile', ARRAY['integer'] ),
    true,
    'is_window(schema, win, arg)',
    'Function pg_catalog.ntile(integer) should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'ntile', ARRAY['integer'] ),
    false,
    'isnt_window(schema, win, arg)',
    'Function pg_catalog.ntile(integer) should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'bah', ARRAY['integer', 'text'] ),
    false,
    'is_window(schema, func, arg)',
    'Function someschema.bah(integer, text) should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'bah', ARRAY['integer', 'text'] ),
    true,
    'isnt_window(schema, func, arg)',
    'Function someschema.bah(integer, text) should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'pg_catalog', 'dense_rank', '{}'::name[] ),
    true,
    'is_window(schema, win, noargs)',
    'Function pg_catalog.dense_rank() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'dense_rank', '{}'::name[] ),
    false,
    'isnt_window(schema, win, noargs)',
    'Function pg_catalog.dense_rank() should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'huh', '{}'::name[] ),
    false,
    'is_window(schema, func, noarg)',
    'Function someschema.huh() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'dense_rank', '{}'::name[] ),
    false,
    'isnt_window(schema, win, noargs)',
    'Function pg_catalog.dense_rank() should not be a window function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'someschema', 'nada', ARRAY['integer'] ),
    false,
    'is_window(schema, nowin, arg)',
    'Function someschema.nada(integer) should be a window function',
    '    Function someschema.nada(integer) does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'nada', ARRAY['integer'] ),
    false,
    'isnt_window(schema, nowin, arg)',
    'Function someschema.nada(integer) should not be a window function',
    '    Function someschema.nada(integer) does not exist'
);

-- is_window ( NAME, NAME, TEXT )
-- isnt_window ( NAME, NAME, TEXT )
SELECT * FROM check_test(
    is_window( 'pg_catalog', 'ntile'::name, 'whatever' ),
    true,
    'is_window(schema, win, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'ntile'::name, 'whatever' ),
    false,
    'isnt_window(schema, win, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'bah'::name, 'whatever' ),
    false,
    'is_window(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'bah'::name, 'whatever' ),
    true,
    'isnt_window(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'huh'::name, 'whatever' ),
    false,
    'is_window(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'dense_rank'::name, 'whatever' ),
    false,
    'isnt_window(schema, win, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'someschema', 'nil'::name, 'whatever' ),
    false,
    'is_window(schema, nowin, desc)',
    'whatever',
    '    Function someschema.nil() does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'nil'::name, 'whatever' ),
    false,
    'isnt_window(schema, nowin, desc)',
    'whatever',
    '    Function someschema.nil() does not exist'
);

-- is_window( NAME, NAME )
-- isnt_window( NAME, NAME )
SELECT * FROM check_test(
    is_window( 'pg_catalog', 'ntile'::name ),
    true,
    'is_window(schema, win)',
    'Function pg_catalog.ntile() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'pg_catalog', 'ntile'::name ),
    false,
    'isnt_window(schema, win)',
    'Function pg_catalog.ntile() should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'someschema', 'bah'::name ),
    false,
    'is_window(schema, func)',
    'Function someschema.bah() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'bah'::name ),
    true,
    'isnt_window(schema, func)',
    'Function someschema.bah() should not be a window function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'someschema', 'zilch'::name ),
    false,
    'is_window(schema, nowin)',
    'Function someschema.zilch() should be a window function',
    '    Function someschema.zilch() does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'someschema', 'zilch'::name ),
    false,
    'isnt_window(schema, nowin)',
    'Function someschema.zilch() should not be a window function',
    '    Function someschema.zilch() does not exist'
);

-- is_window ( NAME, NAME[], TEXT )
-- isnt_window ( NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_window( 'ntile', ARRAY['integer'], 'whatever' ),
    true,
    'is_window(win, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'ntile', ARRAY['integer'], 'whatever' ),
    false,
    'isnt_window(win, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_window(func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_window(func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'dense_rank', '{}'::name[], 'whatever' ),
    true,
    'is_window(win, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'dense_rank', '{}'::name[], 'whatever' ),
    false,
    'isnt_window(win, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'yay', '{}'::name[], 'whatever' ),
    false,
    'is_window(func, noarg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'dense_rank', '{}'::name[], 'whatever' ),
    false,
    'isnt_window(win, noargs, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'nada', ARRAY['integer'], 'whatever' ),
    false,
    'is_window(nowin, arg, desc)',
    'whatever',
    '    Function nada(integer) does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'nada', ARRAY['integer'], 'whatever' ),
    false,
    'isnt_window(nowin, arg, desc)',
    'whatever',
    '    Function nada(integer) does not exist'
);

-- is_window( NAME, NAME[] )
-- isnt_window( NAME, NAME[] )
SELECT * FROM check_test(
    is_window( 'ntile', ARRAY['integer'] ),
    true,
    'is_window(win, arg, desc)',
    'Function ntile(integer) should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'ntile', ARRAY['integer'] ),
    false,
    'isnt_window(win, arg, desc)',
    'Function ntile(integer) should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_window(func, arg, desc)',
    'Function oww(integer, text) should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_window(func, arg, desc)',
    'Function oww(integer, text) should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'dense_rank', '{}'::name[] ),
    true,
    'is_window(win, noargs, desc)',
    'Function dense_rank() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'dense_rank', '{}'::name[] ),
    false,
    'isnt_window(win, noargs, desc)',
    'Function dense_rank() should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'yay', '{}'::name[] ),
    false,
    'is_window(func, noarg, desc)',
    'Function yay() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'dense_rank', '{}'::name[] ),
    false,
    'isnt_window(win, noargs, desc)',
    'Function dense_rank() should not be a window function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'nope', ARRAY['integer'] ),
    false,
    'is_window(nowin, arg, desc)',
    'Function nope(integer) should be a window function',
    '    Function nope(integer) does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'nope', ARRAY['integer'] ),
    false,
    'isnt_window(nowin, arg, desc)',
    'Function nope(integer) should not be a window function',
    '    Function nope(integer) does not exist'
);

-- is_window( NAME, TEXT )
-- isnt_window( NAME, TEXT )
SELECT * FROM check_test(
    is_window( 'ntile'::name, 'whatever' ),
    true,
    'is_window(win, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'lag'::name, 'whatever' ),
    false,
    'isnt_window(win, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'oww'::name, 'whatever' ),
    false,
    'is_window(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_window('oww'::name, 'whatever' ),
    true,
    'isnt_window(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_window( 'yay'::name, 'whatever' ),
    false,
    'is_window(func, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'nonesuch'::name, 'whatever' ),
    false,
    'is_window(nowin, desc)',
    'whatever',
    '    Function nonesuch() does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'nonesuch'::name, 'whatever' ),
    false,
    'isnt_window(nowin, desc)',
    'whatever',
    '    Function nonesuch() does not exist'
);

-- is_window( NAME )
-- isnt_window( NAME )
SELECT * FROM check_test(
    is_window( 'ntile' ),
    true,
    'is_window(win)',
    'Function ntile() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window( 'ntile' ),
    false,
    'isnt_window(win)',
    'Function ntile() should not be a window function',
    ''
);

SELECT * FROM check_test(
    is_window( 'oww' ),
    false,
    'is_window(func)',
    'Function oww() should be a window function',
    ''
);

SELECT * FROM check_test(
    isnt_window('oww' ),
    true,
    'isnt_window(func)',
    'Function oww() should not be a window function',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_window( 'nooo' ),
    false,
    'is_window(nowin)',
    'Function nooo() should be a window function',
    '    Function nooo() does not exist'
);

SELECT * FROM check_test(
    isnt_window( 'nooo' ),
    false,
    'isnt_window(nowin)',
    'Function nooo() should not be a window function',
    '    Function nooo() does not exist'
);

/****************************************************************************/
-- Test is_strict() and isnt_strict().
SELECT * FROM check_test(
    is_strict( 'public', 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_strict(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_strict(schema, func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'public', 'yay', '{}'::name[] ),
    true,
    'is_strict(schema, func, 0 args)',
    'Function public.yay() should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'yay', '{}'::name[] ),
    false,
    'isnt_strict(schema, func, 0 args)',
    'Function public.yay() should not be strict',
    ''
);

SELECT * FROM check_test(
    is_strict( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_strict(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_strict(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_strict(schema, func, args)',
    'Function public.oww(integer, text) should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_strict(schema, func, args)',
    'Function public.oww(integer, text) should not be strict',
    ''
);

SELECT * FROM check_test(
    is_strict( 'public', 'yay', 'whatever' ),
    true,
    'is_strict(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'yay', 'whatever' ),
    false,
    'isnt_strict(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'public', 'yay'::name ),
    true,
    'is_strict(schema, func)',
    'Function public.yay() should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'yay'::name ),
    false,
    'isnt_strict(schema, func)',
    'Function public.yay() should not be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_strict(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'public', 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_strict(schema, func, args)',
    'Function public.oww(integer, text) should not be strict',
    ''
);

SELECT * FROM check_test(
    is_strict( 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_strict(func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'yay', '{}'::name[], 'whatever' ),
    false,
    'isnt_strict(func, 0 args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'yay', '{}'::name[] ),
    true,
    'is_strict(func, 0 args)',
    'Function yay() should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'yay', '{}'::name[] ),
    false,
    'isnt_strict(func, 0 args)',
    'Function yay() should not be strict',
    ''
);

SELECT * FROM check_test(
    is_strict( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_strict(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_strict(func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_strict(func, args)',
    'Function oww(integer, text) should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'oww', ARRAY['integer', 'text'] ),
    true,
    'isnt_strict(func, args)',
    'Function oww(integer, text) should not be strict',
    ''
);

SELECT * FROM check_test(
    is_strict( 'yay', 'whatever' ),
    true,
    'is_strict(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'yay', 'whatever' ),
    false,
    'isnt_strict(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_strict( 'yay'::name ),
    true,
    'is_strict(func)',
    'Function yay() should be strict',
    ''
);

SELECT * FROM check_test(
    isnt_strict( 'yay'::name ),
    false,
    'isnt_strict(func)',
    'Function yay() should not be strict',
    ''
);

/****************************************************************************/
-- Test volatility_is().
SELECT * FROM check_test(
    volatility_is( 'public', 'yay', '{}'::name[], 'volatile', 'whatever' ),
    true,
    'function_volatility(schema, func, 0 args, volatile, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'yay', '{}'::name[], 'v', 'whatever' ),
    true,
    'function_volatility(schema, func, 0 args, v, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'oww', ARRAY['integer', 'text'], 'immutable', 'whatever' ),
    true,
    'function_volatility(schema, func, args, immutable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'pet', '{}'::name[], 'stable', 'whatever' ),
    true,
    'function_volatility(schema, func, 0 args, stable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'yay', '{}'::name[], 'volatile' ),
    true,
    'function_volatility(schema, func, 0 args, volatile)',
    'Function public.yay() should be VOLATILE',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'oww', ARRAY['integer', 'text'], 'immutable' ),
    true,
    'function_volatility(schema, func, args, immutable)',
    'Function public.oww(integer, text) should be IMMUTABLE'
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'yay', 'volatile', 'whatever' ),
    true,
    'function_volatility(schema, func, volatile, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'yay'::name, 'volatile' ),
    true,
    'function_volatility(schema, func, volatile)',
    'Function public.yay() should be VOLATILE',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'oww', 'immutable', 'whatever' ),
    true,
    'function_volatility(schema, func, immutable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'public', 'pet', 'stable', 'whatever' ),
    true,
    'function_volatility(schema, func, stable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'yay', '{}'::name[], 'volatile', 'whatever' ),
    true,
    'function_volatility(func, 0 args, volatile, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'yay', '{}'::name[], 'v', 'whatever' ),
    true,
    'function_volatility(func, 0 args, v, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'oww', ARRAY['integer', 'text'], 'immutable', 'whatever' ),
    true,
    'function_volatility(func, args, immutable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'pet', '{}'::name[], 'stable', 'whatever' ),
    true,
    'function_volatility(func, 0 args, stable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'yay', '{}'::name[], 'volatile' ),
    true,
    'function_volatility(func, 0 args, volatile)',
    'Function yay() should be VOLATILE',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'oww', ARRAY['integer', 'text'], 'immutable' ),
    true,
    'function_volatility(func, args, immutable)',
    'Function oww(integer, text) should be IMMUTABLE'
    ''
);

SELECT * FROM check_test(
    volatility_is( 'yay', 'volatile', 'whatever' ),
    true,
    'function_volatility(func, volatile, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'yay'::name, 'volatile' ),
    true,
    'function_volatility(func, volatile)',
    'Function yay() should be VOLATILE',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'oww', 'immutable', 'whatever' ),
    true,
    'function_volatility(func, immutable, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    volatility_is( 'pet', 'stable', 'whatever' ),
    true,
    'function_volatility(func, stable, desc)',
    'whatever',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
