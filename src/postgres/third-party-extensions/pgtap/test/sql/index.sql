\unset ECHO
\i test/setup.sql

SELECT plan(270);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE INDEX idx_hey ON public.sometab(numb);
SET client_min_messages = error;
CREATE INDEX idx_foo ON public.sometab using hash(name);
SET client_min_messages = warning;
CREATE INDEX idx_bar ON public.sometab(numb, name);
CREATE UNIQUE INDEX idx_baz ON public.sometab(LOWER(name));
CREATE INDEX idx_mul ON public.sometab(numb, LOWER(name));
CREATE INDEX idx_expr ON public.sometab(UPPER(name), numb, LOWER(name));
CREATE INDEX idx_double ON public.sometab(numb, myint);
RESET client_min_messages;

/****************************************************************************/
-- Test has_index().

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_hey', 'numb', 'whatever' ),
    true,
    'has_index() single column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_hey', 'numb'::name ),
    true,
    'has_index() single column no desc',
    'Index idx_hey should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo', 'name', 'whatever' ),
    true,
    'has_index() hash index',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo', 'name'::name ),
    true,
    'has_index() hash index no desc',
    'Index idx_foo should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['numb', 'name'], 'whatever' ),
    true,
    'has_index() multi-column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['numb', 'name'] ),
    true,
    'has_index() multi-column no desc',
    'Index idx_bar should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', 'lower(name)', 'whatever' ),
    true,
    'has_index() functional',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', ARRAY['lower(name)'], 'whatever' ),
    true,
    'has_index() [functional]',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_mul', ARRAY['numb', 'lower(name)'], 'whatever' ),
    true,
    'has_index() [col, expr]',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index(
        'public', 'sometab', 'idx_expr',
        ARRAY['upper(name)', 'numb', 'lower(name)'],
        'whatever'
    ),
    true,
    'has_index() [expr, col, expr]',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', 'whatever' ),
    true,
    'has_index() no cols',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo', 'whatever' ),
    true,
    'has_index() hash index',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz'::name ),
    true,
    'has_index() no cols no desc',
    'Index idx_baz should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo'::name ),
    true,
    'has_index() no cols hash index no desc',
    'Index idx_foo should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_foo', 'name', 'whatever' ),
    true,
    'has_index() no schema single column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_foo', 'name'::name ),
    true,
    'has_index() no schema single column no desc',
    'Index idx_foo should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['numb', 'name'], 'whatever' ),
    true,
    'has_index() no schema multi-column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['numb', 'name'] ),
    true,
    'has_index() no schema multi-column no desc',
    'Index idx_bar should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'lower(name)', 'whatever' ),
    true,
    'has_index() no schema functional',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'lower(name)' ),
    true,
    'has_index() no schema functional no desc',
    'Index idx_baz should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'whatever' ),
    true,
    'has_index() no schema or cols',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_foo', 'whatever' ),
    true,
    'has_index() hash index no schema or cols',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz' ),
    true,
    'has_index() no schema or cols or desc',
    'Index idx_baz should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_foo' ),
    true,
    'has_index() hash index no schema or cols or desc',
    'Index idx_foo should exist',
    ''
);

-- Check failure diagnostics.
SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_heya', 'numb', 'whatever' ),
    false,
    'has_index() non-existent',
    'whatever',
    'Index idx_heya ON public.sometab not found'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'blah', ARRAY['numb', 'name'], 'whatever' ),
    false,
    'has_index() missing',
    'whatever',
    'Index blah ON public.sometab not found'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['name', 'id'], 'whatever' ),
    false,
    'has_index() invalid',
    'whatever',
    '        have: idx_bar ON public.sometab(numb, name)
        want: idx_bar ON public.sometab(name, id)'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['name'], 'whatever' ),
    false,
    'has_index() missing column',
    'whatever',
    '        have: idx_bar ON public.sometab(numb, name)
        want: idx_bar ON public.sometab(name)'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'blah', ARRAY['numb', 'name'], 'whatever' ),
    false,
    'has_index() missing no schema',
    'whatever',
    'Index blah ON sometab not found'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['name', 'id'], 'whatever' ),
    false,
    'has_index() invalid no schema',
    'whatever',
    '        have: idx_bar ON sometab(numb, name)
        want: idx_bar ON sometab(name, id)'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', 'lower(wank)', 'whatever' ),
    false,
    'has_index() functional fail',
    'whatever',
    '        have: idx_baz ON public.sometab(lower(name))
        want: idx_baz ON public.sometab(lower(wank))'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'lower(wank)', 'whatever' ),
    false,
    'has_index() functional fail no schema',
    'whatever',
    '        have: idx_baz ON sometab(lower(name))
        want: idx_baz ON sometab(lower(wank))'
);

/****************************************************************************/
-- Test hasnt_index().

SELECT * FROM check_test(
    hasnt_index( 'public', 'sometab', 'idx_foo', 'whatever' ),
    false,
    'hasnt_index(schema, table, index, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'public', 'sometab', 'idx_foo'::name ),
    false,
    'hasnt_index(schema, table, index)',
    'Index idx_foo should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'public', 'sometab', 'idx_blah', 'whatever' ),
    true,
    'hasnt_index(schema, table, non-index, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'public', 'sometab', 'idx_blah'::name ),
    true,
    'hasnt_index(schema, table, non-index)',
    'Index idx_blah should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'sometab', 'idx_foo', 'whatever' ),
    false,
    'hasnt_index(table, index, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'sometab', 'idx_foo'::name ),
    false,
    'hasnt_index(table, index)',
    'Index idx_foo should not exist',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'sometab', 'idx_blah', 'whatever' ),
    true,
    'hasnt_index(table, non-index, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_index( 'sometab', 'idx_blah'::name ),
    true,
    'hasnt_index(table, non-index)',
    'Index idx_blah should not exist',
    ''
);

/****************************************************************************/
-- Test index_is_unique().
SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'idx_baz', 'whatever' ),
    true,
    'index_is_unique()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'idx_baz' ),
    true,
    'index_is_unique() no desc',
    'Index idx_baz should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'sometab', 'idx_baz' ),
    true,
    'index_is_unique() no schema',
    'Index idx_baz should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'idx_baz' ),
    true,
    'index_is_unique() index only',
    'Index idx_baz should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'sometab_pkey', 'whatever' ),
    true,
    'index_is_unique() on pk',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'sometab_pkey' ),
    true,
    'index_is_unique() on pk no desc',
    'Index sometab_pkey should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'sometab', 'sometab_pkey' ),
    true,
    'index_is_unique() on pk no schema',
    'Index sometab_pkey should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'sometab_pkey' ),
    true,
    'index_is_unique() on pk index only',
    'Index sometab_pkey should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'idx_bar', 'whatever' ),
    false,
    'index_is_unique() fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'public', 'sometab', 'idx_bar' ),
    false,
    'index_is_unique() fail no desc',
    'Index idx_bar should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'sometab', 'idx_bar' ),
    false,
    'index_is_unique() fail no schema',
    'Index idx_bar should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'idx_bar' ),
    false,
    'index_is_unique() fail index only',
    'Index idx_bar should be unique',
    ''
);

SELECT * FROM check_test(
    index_is_unique( 'blahblah' ),
    false,
    'index_is_unique() no such index',
    'Index blahblah should be unique',
    ''
);

/****************************************************************************/
-- Test index_is_primary().
SELECT * FROM check_test(
    index_is_primary( 'public', 'sometab', 'sometab_pkey', 'whatever' ),
    true,
    'index_is_primary()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'public', 'sometab', 'sometab_pkey' ),
    true,
    'index_is_primary() no desc',
    'Index sometab_pkey should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'sometab', 'sometab_pkey' ),
    true,
    'index_is_primary() no schema',
    'Index sometab_pkey should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'sometab_pkey' ),
    true,
    'index_is_primary() index only',
    'Index sometab_pkey should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'public', 'sometab', 'idx_baz', 'whatever' ),
    false,
    'index_is_primary() fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'public', 'sometab', 'idx_baz' ),
    false,
    'index_is_primary() fail no desc',
    'Index idx_baz should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'sometab', 'idx_baz' ),
    false,
    'index_is_primary() fail no schema',
    'Index idx_baz should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'idx_baz' ),
    false,
    'index_is_primary() fail index only',
    'Index idx_baz should be on a primary key',
    ''
);

SELECT * FROM check_test(
    index_is_primary( 'blahblah' ),
    false,
    'index_is_primary() no such index',
    'Index blahblah should be on a primary key',
    ''
);

/****************************************************************************/
-- Test is_clustered().
SELECT * FROM check_test(
    is_clustered( 'public', 'sometab', 'idx_bar', 'whatever' ),
    false,
    'is_clustered() fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'public', 'sometab', 'idx_bar' ),
    false,
    'is_clustered() fail no desc',
    'Table public.sometab should be clustered on index idx_bar',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'sometab', 'idx_bar' ),
    false,
    'is_clustered() fail no schema',
    'Table sometab should be clustered on index idx_bar',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'idx_bar' ),
    false,
    'is_clustered() fail index only',
    'Table should be clustered on index idx_bar',
    ''
);

CLUSTER idx_bar ON public.sometab;
SELECT * FROM check_test(
    is_clustered( 'public', 'sometab', 'idx_bar', 'whatever' ),
    true,
    'is_clustered()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'public', 'sometab', 'idx_bar' ),
    true,
    'is_clustered() no desc',
    'Table public.sometab should be clustered on index idx_bar',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'sometab', 'idx_bar' ),
    true,
    'is_clustered() no schema',
    'Table sometab should be clustered on index idx_bar',
    ''
);

SELECT * FROM check_test(
    is_clustered( 'idx_bar' ),
    true,
    'is_clustered() index only',
    'Table should be clustered on index idx_bar',
    ''
);

/****************************************************************************/
-- Test index_is_type().
SELECT * FROM check_test(
    index_is_type( 'public', 'sometab', 'idx_bar', 'btree', 'whatever' ),
    true,
    'index_is_type()',
    'whatever',
    ''
);

SELECT * FROM check_test(
    index_is_type( 'public', 'sometab', 'idx_bar', 'btree' ),
    true,
    'index_is_type() no desc',
    'Index idx_bar should be a btree index',
    ''
);

SELECT * FROM check_test(
    index_is_type( 'public', 'sometab', 'idx_bar', 'hash' ),
    false,
    'index_is_type() fail',
    'Index idx_bar should be a hash index',
    '        have: btree
        want: hash'
);

SELECT * FROM check_test(
    index_is_type( 'sometab', 'idx_bar', 'btree' ),
    true,
    'index_is_type() no schema',
    'Index idx_bar should be a btree index',
    ''
);

SELECT * FROM check_test(
    index_is_type( 'sometab', 'idx_bar', 'hash' ),
    false,
    'index_is_type() no schema fail',
    'Index idx_bar should be a hash index',
    '        have: btree
        want: hash'
);

SELECT * FROM check_test(
    index_is_type( 'idx_bar', 'btree' ),
    true,
    'index_is_type() no table',
    'Index idx_bar should be a btree index',
    ''
);

SELECT * FROM check_test(
    index_is_type( 'idx_bar', 'hash' ),
    false,
    'index_is_type() no table fail',
    'Index idx_bar should be a hash index',
    '        have: btree
        want: hash'
);

SELECT * FROM check_test(
    index_is_type( 'idx_foo', 'hash' ),
    true,
    'index_is_type() hash',
    'Index idx_foo should be a hash index',
    ''
);

/****************************************************************************/
-- Test is_indexed().
SELECT * FROM check_test(
    is_indexed( 'public', 'sometab', ARRAY['numb','name']::name[], 'desc' ),
    true,
    'is_indexed( schema, table, columns[], description )',
    'desc',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab', ARRAY['numb','name']::name[] ),
    true,
    'is_indexed( schema, table, columns[] )',
    'Should have an index on public.sometab(numb, name)',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'sometab', ARRAY['numb','name']::name[], 'desc' ),
    true,
    'is_indexed( table, columns[], description )',
    'desc',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'sometab', ARRAY['numb','name']::name[] ),
    true,
    'is_indexed( table, columns[] )',
    'Should have an index on sometab(numb, name)',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab', 'numb', 'desc' ),
    true,
    'is_indexed( schema, table, column, description )',
    'desc',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab'::name, 'numb'::name ),
    true,
    'is_indexed( schema, table, column )',
    'Should have an index on public.sometab(numb)',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab'::name, 'myint'::name ),
    false,
    'is_indexed( schema, table, column ) fail',
    'Should have an index on public.sometab(myint)',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab', ARRAY['name','numb']::name[] ),
    false,
    'is_indexed( schema, table, columns[] ) fail, column order matters',
    'Should have an index on public.sometab(name, numb)',
    ''
);


-- Make sure index expressions are supported.
SELECT * FROM check_test(
    is_indexed(
        'public', 'sometab',
        ARRAY['upper(name)', 'numb', 'lower(name)'],
        'whatever'
    ),
    true,
    'is_indexed(schema, table, expressions)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'public', 'sometab', 'lower(name)', 'whatever' ),
    true,
    'is_indexed(schema, table, expression)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_indexed(
        'sometab',
        ARRAY['upper(name)', 'numb', 'lower(name)'],
        'whatever'
    ),
    true,
    'is_indexed(table, expressions)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    is_indexed( 'sometab', 'lower(name)'::name, 'whatever' ),
    true,
    'is_indexed( table, expression)',
    'whatever',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
