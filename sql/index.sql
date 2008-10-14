\unset ECHO
\i test_setup.sql

-- $Id$

SELECT plan(63);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '' UNIQUE,
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE INDEX idx_foo ON public.sometab(name);
CREATE INDEX idx_bar ON public.sometab(name, numb);
CREATE INDEX idx_baz ON public.sometab(LOWER(name));
RESET client_min_messages;

/****************************************************************************/
-- Test has_index().

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo', 'name', 'whatever' ),
    true,
    'has_index() single column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_foo', 'name'::name ),
    true,
    'has_index() single column no desc',
    'Index "idx_foo" should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['name', 'numb'], 'whatever' ),
    true,
    'has_index() multi-column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['name', 'numb'] ),
    true,
    'has_index() multi-column no desc',
    'Index "idx_bar" should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', 'LOWER(name)', 'whatever' ),
    true,
    'has_index() functional',
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
    has_index( 'public', 'sometab', 'idx_baz'::name ),
    true,
    'has_index() no cols no desc',
    'Index "idx_baz" should exist',
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
    'Index "idx_foo" should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['name', 'numb'], 'whatever' ),
    true,
    'has_index() no schema multi-column',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['name', 'numb'] ),
    true,
    'has_index() no schema multi-column no desc',
    'Index "idx_bar" should exist',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'LOWER(name)', 'whatever' ),
    true,
    'has_index() no schema functional',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'LOWER(name)' ),
    true,
    'has_index() no schema functional no desc',
    'Index "idx_baz" should exist',
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
    has_index( 'sometab', 'idx_baz' ),
    true,
    'has_index() no schema or cols or desc',
    'Index "idx_baz" should exist',
    ''
);

-- Check failure diagnostics.
SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'blah', ARRAY['name', 'numb'], 'whatever' ),
    false,
    'has_index() missing',
    'whatever',
    'Index "blah" ON public.sometab not found'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_bar', ARRAY['name', 'id'], 'whatever' ),
    false,
    'has_index() invalid',
    'whatever',
    '       have: "idx_bar" ON public.sometab(name, numb)
        want: "idx_bar" ON public.sometab(name, id)'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'blah', ARRAY['name', 'numb'], 'whatever' ),
    false,
    'has_index() missing no schema',
    'whatever',
    'Index "blah" ON sometab not found'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_bar', ARRAY['name', 'id'], 'whatever' ),
    false,
    'has_index() invalid no schema',
    'whatever',
    '       have: "idx_bar" ON sometab(name, numb)
        want: "idx_bar" ON sometab(name, id)'
);

SELECT * FROM check_test(
    has_index( 'public', 'sometab', 'idx_baz', 'LOWER(wank)', 'whatever' ),
    false,
    'has_index() functional fail',
    'whatever',
    '       have: "idx_baz" ON public.sometab(lower(name))
        want: "idx_baz" ON public.sometab(lower(wank))'
);

SELECT * FROM check_test(
    has_index( 'sometab', 'idx_baz', 'LOWER(wank)', 'whatever' ),
    false,
    'has_index() functional fail no schema',
    'whatever',
    '       have: "idx_baz" ON sometab(lower(name))
        want: "idx_baz" ON sometab(lower(wank))'
);


/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
