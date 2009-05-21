\unset ECHO
\i test_setup.sql

SELECT plan(72);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TYPE public.bug_status AS ENUM ('new', 'open', 'closed');
RESET client_min_messages;

/****************************************************************************/
-- Make sure has_type for enums.
SELECT * FROM check_test(
    has_type( 'bug_status' ),
    true,
    'has_type(enum)',
    'Type bug_status should exist',
    ''
);

/****************************************************************************/
-- Test has_enum().
SELECT * FROM check_test(
    has_enum( 'bug_status' ),
    true,
    'has_enum(enum)',
    'Enum bug_status should exist',
    ''
);

SELECT * FROM check_test(
    has_enum( 'bug_status', 'mydesc' ),
    true,
    'has_enum(enum, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_enum( 'public'::name, 'bug_status'::name ),
    true,
    'has_enum(scheam, enum)',
    'Enum public.bug_status should exist',
    ''
);
SELECT * FROM check_test(
    has_enum( 'public', 'bug_status', 'mydesc' ),
    true,
    'has_enum(schema, enum, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    has_enum( '__foobarbaz__' ),
    false,
    'has_enum(enum)',
    'Enum __foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_enum( '__foobarbaz__', 'mydesc' ),
    false,
    'has_enum(enum, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    has_enum( 'public'::name, '__foobarbaz__'::name ),
    false,
    'has_enum(scheam, enum)',
    'Enum public.__foobarbaz__ should exist',
    ''
);
SELECT * FROM check_test(
    has_enum( 'public', '__foobarbaz__', 'mydesc' ),
    false,
    'has_enum(schema, enum, desc)',
    'mydesc',
    ''
);

/****************************************************************************/
-- Test hasnt_enum().
SELECT * FROM check_test(
    hasnt_enum( '__foobarbaz__' ),
    true,
    'hasnt_enum(enum)',
    'Enum __foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_enum(enum, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( 'public'::name, '__foobarbaz__'::name ),
    true,
    'hasnt_enum(scheam, enum)',
    'Enum public.__foobarbaz__ should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( 'public', '__foobarbaz__', 'mydesc' ),
    true,
    'hasnt_enum(schema, enum, desc)',
    'mydesc',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    hasnt_enum( 'bug_status' ),
    false,
    'hasnt_enum(enum)',
    'Enum bug_status should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( 'bug_status', 'mydesc' ),
    false,
    'hasnt_enum(enum, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( 'public'::name, 'bug_status'::name ),
    false,
    'hasnt_enum(scheam, enum)',
    'Enum public.bug_status should not exist',
    ''
);
SELECT * FROM check_test(
    hasnt_enum( 'public', 'bug_status', 'mydesc' ),
    false,
    'hasnt_enum(schema, enum, desc)',
    'mydesc',
    ''
);

/****************************************************************************/
-- Test enum_has_labels().
SELECT * FROM check_test(
    enum_has_labels( 'public', 'bug_status', ARRAY['new', 'open', 'closed'], 'mydesc' ),
    true,
    'enum_has_labels(schema, enum, labels, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    enum_has_labels( 'public', 'bug_status', ARRAY['new', 'open', 'closed'] ),
    true,
    'enum_has_labels(schema, enum, labels)',
    'Enum public.bug_status should have labels (new, open, closed)',
    ''
);
SELECT * FROM check_test(
    enum_has_labels( 'bug_status', ARRAY['new', 'open', 'closed'], 'mydesc' ),
    true,
    'enum_has_labels(enum, labels, desc)',
    'mydesc',
    ''
);
SELECT * FROM check_test(
    enum_has_labels( 'bug_status', ARRAY['new', 'open', 'closed'] ),
    true,
    'enum_has_labels(enum, labels)',
    'Enum bug_status should have labels (new, open, closed)',
    ''
);

-- Try failures.
SELECT * FROM check_test(
    enum_has_labels( 'public', 'bug_status', ARRAY['new', 'closed', 'open'], 'mydesc' ),
    false,
    'enum_has_labels(schema, enum, labels, desc) fail',
    'mydesc',
    '        have: {new,open,closed}
        want: {new,closed,open}'
);
SELECT * FROM check_test(
    enum_has_labels( 'public', 'bug_status', ARRAY['new', 'open', 'Closed'], 'mydesc' ),
    false,
    'enum_has_labels(schema, enum, labels, desc) fail',
    'mydesc',
    '        have: {new,open,closed}
        want: {new,open,Closed}'
);
SELECT * FROM check_test(
    enum_has_labels( 'bug_status', ARRAY['new', 'closed', 'open'], 'mydesc' ),
    false,
    'enum_has_labels(enum, labels, desc) fail',
    'mydesc',
    '        have: {new,open,closed}
        want: {new,closed,open}'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
