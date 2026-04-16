\unset ECHO
\i test/setup.sql

SELECT plan(192);
-- SELECT * FROM no_plan();

CREATE SCHEMA procschema;
CREATE FUNCTION procschema.afunc() RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;
CREATE FUNCTION procschema.argfunc(int, text) RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;

CREATE PROCEDURE procschema.aproc() AS 'SELECT TRUE' LANGUAGE SQL;
CREATE PROCEDURE procschema.argproc(int, text) AS 'SELECT TRUE' LANGUAGE SQL;

CREATE FUNCTION public.pubfunc() RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;
CREATE FUNCTION public.argpubfunc(int, text) RETURNS BOOL AS 'SELECT TRUE' LANGUAGE SQL;

CREATE PROCEDURE public.pubproc() AS 'SELECT TRUE' LANGUAGE SQL;
CREATE PROCEDURE public.argpubproc(int, text) AS 'SELECT TRUE' LANGUAGE SQL;

-- is_procedure ( NAME, NAME, NAME[], TEXT )
-- isnt_procedure ( NAME, NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_procedure( 'procschema', 'aproc', '{}'::name[], 'whatever' ),
    true,
    'is_procedure(schema, proc, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'aproc', '{}'::name[], 'whatever' ),
    false,
    'isnt_procedure(schema, proc, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'afunc', '{}'::name[], 'whatever' ),
    false,
    'is_procedure(schema, func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'afunc', '{}'::name[], 'whatever' ),
    true,
    'isnt_procedure(schema, func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argproc', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'is_procedure(schema, proc, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argproc', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'isnt_procedure(schema, proc, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argfunc', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_procedure(schema, func, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argfunc', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_procedure(schema, func, args, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'procschema', 'nope', '{}'::name[], 'whatever' ),
    false,
    'is_procedure(schema, noproc, noargs, desc)',
    'whatever',
    '    Function procschema.nope() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'nope', '{}'::name[], 'whatever' ),
    false,
    'isnt_procedure(schema, noproc, noargs, desc)',
    'whatever',
    '    Function procschema.nope() does not exist'
);

-- is_procedure( NAME, NAME, NAME[] )
-- isnt_procedure( NAME, NAME, NAME[] )
SELECT * FROM check_test(
    is_procedure( 'procschema', 'aproc', '{}'::name[] ),
    true,
    'is_procedure(schema, proc, noargs)',
    'Function procschema.aproc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'aproc', '{}'::name[] ),
    false,
    'isnt_procedure(schema, proc, noargs)',
    'Function procschema.aproc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'afunc', '{}'::name[] ),
    false,
    'is_procedure(schema, func, noargs)',
    'Function procschema.afunc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'afunc', '{}'::name[] ),
    true,
    'isnt_procedure(schema, func, noargs)',
    'Function procschema.afunc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argproc', ARRAY['integer', 'text'] ),
    true,
    'is_procedure(schema, proc, args)',
    'Function procschema.argproc(integer, text) should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argproc', ARRAY['integer', 'text'] ),
    false,
    'isnt_procedure(schema, proc, args)',
    'Function procschema.argproc(integer, text) should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argfunc', ARRAY['integer', 'text'] ),
    false,
    'is_procedure(schema, func, args)',
    'Function procschema.argfunc(integer, text) should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argfunc', ARRAY['integer', 'text'] ),
    true,
    'isnt_procedure(schema, func, args)',
    'Function procschema.argfunc(integer, text) should not be a procedure',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'procschema', 'nada', '{}'::name[] ),
    false,
    'is_procedure(schema, noproc, noargs)',
    'Function procschema.nada() should be a procedure',
    '    Function procschema.nada() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'nada', '{}'::name[] ),
    false,
    'isnt_procedure(schema, noproc, noargs)',
    'Function procschema.nada() should not be a procedure',
    '    Function procschema.nada() does not exist'
);

-- is_procedure ( NAME, NAME, TEXT )
-- isnt_procedure ( NAME, NAME, TEXT )
SELECT * FROM check_test(
    is_procedure( 'procschema', 'argproc', 'whatever' ),
    true,
    'is_procedure(schema, proc, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argproc', 'whatever' ),
    false,
    'isnt_procedure(schema, proc, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argfunc', 'whatever' ),
    false,
    'is_procedure(schema, func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argfunc', 'whatever' ),
    true,
    'isnt_procedure(schema, func, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'procschema', 'nork', 'whatever' ),
    false,
    'is_procedure(schema, noproc, desc)',
    'whatever',
    '    Function procschema.nork() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'nork', 'whatever' ),
    false,
    'isnt_procedure(schema, noproc, desc)',
    'whatever',
    '    Function procschema.nork() does not exist'
);

-- is_procedure( NAME, NAME )
-- isnt_procedure( NAME, NAME )
SELECT * FROM check_test(
    is_procedure( 'procschema', 'argproc'::name ),
    true,
    'is_procedure(schema, proc)',
    'Function procschema.argproc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argproc'::name ),
    false,
    'isnt_procedure(schema, proc)',
    'Function procschema.argproc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'procschema', 'argfunc'::name ),
    false,
    'is_procedure(schema, func)',
    'Function procschema.argfunc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'argfunc'::name ),
    true,
    'isnt_procedure(schema, func)',
    'Function procschema.argfunc() should not be a procedure',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'procschema', 'zippo'::name ),
    false,
    'is_procedure(schema, noproc)',
    'Function procschema.zippo() should be a procedure',
    '    Function procschema.zippo() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'procschema', 'zippo'::name ),
    false,
    'isnt_procedure(schema, noproc)',
    'Function procschema.zippo() should not be a procedure',
    '    Function procschema.zippo() does not exist'
);


-- is_procedure ( NAME, NAME[], TEXT )
-- isnt_procedure ( NAME, NAME[], TEXT )
SELECT * FROM check_test(
    is_procedure( 'pubproc', '{}'::name[], 'whatever' ),
    true,
    'is_procedure(proc, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'pubproc', '{}'::name[], 'whatever' ),
    false,
    'isnt_procedure(proc, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'pubfunc', '{}'::name[], 'whatever' ),
    false,
    'is_procedure(func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'pubfunc', '{}'::name[], 'whatever' ),
    true,
    'isnt_procedure(schema, func, noargs, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubproc', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'is_procedure(proc, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubproc', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'isnt_procedure(proc, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubfunc', ARRAY['integer', 'text'], 'whatever' ),
    false,
    'is_procedure(unc, args, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubfunc', ARRAY['integer', 'text'], 'whatever' ),
    true,
    'isnt_procedure(func, args, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'nonesuch', '{}'::name[], 'whatever' ),
    false,
    'is_procedure(noproc, noargs, desc)',
    'whatever',
    '    Function nonesuch() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'nonesuch', '{}'::name[], 'whatever' ),
    false,
    'isnt_procedure(noproc, noargs, desc)',
    'whatever',
    '    Function nonesuch() does not exist'
);

-- is_procedure( NAME, NAME[] )
-- isnt_procedure( NAME, NAME[] )
SELECT * FROM check_test(
    is_procedure( 'pubproc', '{}'::name[] ),
    true,
    'is_procedure(proc, noargs)',
    'Function pubproc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'pubproc', '{}'::name[] ),
    false,
    'isnt_procedure(proc, noargs)',
    'Function pubproc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'pubfunc', '{}'::name[] ),
    false,
    'is_procedure(func, noargs)',
    'Function pubfunc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'pubfunc', '{}'::name[] ),
    true,
    'isnt_procedure(schema, func, noargs)',
    'Function pubfunc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubproc', ARRAY['integer', 'text'] ),
    true,
    'is_procedure(proc, args)',
    'Function argpubproc(integer, text) should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubproc', ARRAY['integer', 'text'] ),
    false,
    'isnt_procedure(proc, args)',
    'Function argpubproc(integer, text) should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubfunc', ARRAY['integer', 'text'] ),
    false,
    'is_procedure(unc, args)',
    'Function argpubfunc(integer, text) should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubfunc', ARRAY['integer', 'text'] ),
    true,
    'isnt_procedure(func, args)',
    'Function argpubfunc(integer, text) should not be a procedure',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'nix', '{}'::name[] ),
    false,
    'is_procedure(noproc, noargs)',
    'Function nix() should be a procedure',
    '    Function nix() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'nix', '{}'::name[] ),
    false,
    'isnt_procedure(noproc, noargs)',
    'Function nix() should not be a procedure',
    '    Function nix() does not exist'
);

-- is_procedure( NAME, TEXT )
-- isnt_procedure( NAME, TEXT )
SELECT * FROM check_test(
    is_procedure( 'argpubproc', 'whatever' ),
    true,
    'is_procedure(proc, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubproc', 'whatever' ),
    false,
    'isnt_procedure(proc, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubfunc', 'whatever' ),
    false,
    'is_procedure(func, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubfunc', 'whatever' ),
    true,
    'isnt_procedure(func, desc)',
    'whatever',
    ''
);

-- Test diagnostics
SELECT * FROM check_test(
    is_procedure( 'zilch', 'whatever' ),
    false,
    'is_procedure(noproc, desc)',
    'whatever',
    '    Function zilch() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'zilch', 'whatever' ),
    false,
    'isnt_procedure(noproc, desc)',
    'whatever',
    '    Function zilch() does not exist'
);

-- is_procedure( NAME )
-- isnt_procedure( NAME )
SELECT * FROM check_test(
    is_procedure( 'argpubproc' ),
    true,
    'is_procedure(proc)',
    'Function argpubproc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubproc' ),
    false,
    'isnt_procedure(proc)',
    'Function argpubproc() should not be a procedure',
    ''
);

SELECT * FROM check_test(
    is_procedure( 'argpubfunc' ),
    false,
    'is_procedure(func)',
    'Function argpubfunc() should be a procedure',
    ''
);

SELECT * FROM check_test(
    isnt_procedure( 'argpubfunc' ),
    true,
    'isnt_procedure(func)',
    'Function argpubfunc() should not be a procedure',
    ''
);

-- Test diagnosics
SELECT * FROM check_test(
    is_procedure( 'nope' ),
    false,
    'is_procedure(noproc)',
    'Function nope() should be a procedure',
    '    Function nope() does not exist'
);

SELECT * FROM check_test(
    isnt_procedure( 'nope' ),
    false,
    'isnt_procedure(noproc)',
    'Function nope() should not be a procedure',
    '    Function nope() does not exist'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
