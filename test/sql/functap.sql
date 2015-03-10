\unset ECHO
\i test/setup.sql

SELECT plan(520);
--SELECT * FROM no_plan();

CREATE SCHEMA someschema;
CREATE FUNCTION someschema.huh () RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;
CREATE FUNCTION someschema.bah (int, text) RETURNS BOOL
AS 'BEGIN RETURN TRUE; END;' LANGUAGE plpgsql;

CREATE FUNCTION public.yay () RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL STRICT SECURITY DEFINER VOLATILE;
CREATE FUNCTION public.oww (int, text) RETURNS BOOL
AS 'BEGIN RETURN TRUE; END;' LANGUAGE plpgsql IMMUTABLE;
CREATE FUNCTION public.pet () RETURNS SETOF BOOL
AS 'BEGIN RETURN NEXT TRUE; RETURN; END;' LANGUAGE plpgsql STABLE;

CREATE AGGREGATE public.tap_accum (
    sfunc    = array_append,
    basetype = anyelement,
    stype    = anyarray,
    initcond = '{}'
);

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
    'simple scchma.func with 0 args, desc',
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
    has_function( 'array_cat', ARRAY['anyarray','anyarray'] ),
    true,
    'simple array function',
    'Function array_cat(anyarray, anyarray) should exist',
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
    'simple scchma.func with 0 args, desc',
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
    hasnt_function( 'array_cat', ARRAY['anyarray','anyarray'] ),
    false,
    'simple array function',
    'Function array_cat(anyarray, anyarray) should not exist',
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
-- Test is_definer().
SELECT * FROM check_test(
    is_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(schema, func, 0 args, desc)',
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
    is_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(schema, func, args, desc)',
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
    is_definer( 'public', 'yay', 'whatever' ),
    true,
    'is_definer(schema, func, desc)',
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
    is_definer( 'public', 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(schema, func, 0 args, desc)',
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
    is_definer( 'public', 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(schema, func, args, desc)',
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
    is_definer( 'public', 'yay', 'whatever' ),
    true,
    'is_definer(schema, func, desc)',
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
    is_definer( 'yay', '{}'::name[], 'whatever' ),
    true,
    'is_definer(func, 0 args, desc)',
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
    is_definer( 'oww', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_definer(func, args, desc)',
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
    is_definer( 'yay', 'whatever' ),
    true,
    'is_definer(func, desc)',
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

/****************************************************************************/
-- Test is_aggregate().
SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY['anyelement'], 'whatever' ),
    true,
    'is_aggregate(schema, func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY['anyelement'] ),
    true,
    'is_aggregate(schema, func, arg)',
    'Function public.tap_accum(anyelement) should be an aggregate function',
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
    is_aggregate( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_aggregate(schema, func, args)',
    'Function public.oww(integer, text) should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', 'whatever' ),
    true,
    'is_aggregate(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum'::name ),
    true,
    'is_aggregate(schema, func)',
    'Function public.tap_accum() should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY['anyelement'], 'whatever' ),
    true,
    'is_aggregate(schema, func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', ARRAY['anyelement'] ),
    true,
    'is_aggregate(schema, func, arg)',
    'Function public.tap_accum(anyelement) should be an aggregate function',
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
    is_aggregate( 'public', 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_aggregate(schema, func, args)',
    'Function public.oww(integer, text) should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum', 'whatever' ),
    true,
    'is_aggregate(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'public', 'tap_accum'::name ),
    true,
    'is_aggregate(schema, func)',
    'Function public.tap_accum() should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'tap_accum', ARRAY['anyelement'], 'whatever' ),
    true,
    'is_aggregate(func, arg, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'tap_accum', ARRAY['anyelement'] ),
    true,
    'is_aggregate(func, arg)',
    'Function tap_accum(anyelement) should be an aggregate function',
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
    is_aggregate( 'oww', ARRAY['integer', 'text'] ),
    false,
    'is_aggregate(func, args)',
    'Function oww(integer, text) should be an aggregate function',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'tap_accum', 'whatever' ),
    true,
    'is_aggregate(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_aggregate( 'tap_accum'::name ),
    true,
    'is_aggregate(func)',
    'Function tap_accum() should be an aggregate function',
    ''
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
    'is_strict(schema, func, args, desc)',
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
    'is_strict(schema, func, args)',
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
    volatility_is( 'public', 'yay', '{}'::name[], 'VOLATILE', 'whatever' ),
    true,
    'function_volatility(schema, func, 0 args, VOLATILE, desc)',
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
    volatility_is( 'yay', '{}'::name[], 'VOLATILE', 'whatever' ),
    true,
    'function_volatility(func, 0 args, VOLATILE, desc)',
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
