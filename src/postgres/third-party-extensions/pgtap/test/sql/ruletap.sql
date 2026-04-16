\unset ECHO
\i test/setup.sql

SELECT plan(132);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.sometab(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT '',
    numb  NUMERIC(10, 2),
    myint NUMERIC(8)
);
CREATE RULE ins_me AS ON INSERT TO public.sometab DO NOTHING;
CREATE RULE upd_me AS ON UPDATE TO public.sometab DO ALSO SELECT now();

CREATE TABLE public.sprockets ( id INT );
CREATE RULE ins_me AS ON INSERT TO public.sprockets DO INSTEAD NOTHING;
CREATE RULE del_me AS ON delete TO public.sprockets DO INSTEAD NOTHING;

CREATE TABLE public.widgets (id int);
CREATE RULE del_me AS ON DELETE TO public.widgets DO NOTHING;
CREATE RULE ins_me AS ON INSERT TO public.widgets DO NOTHING;

RESET client_min_messages;

/****************************************************************************/
-- Test has_rule() and hasnt_rule().

SELECT * FROM check_test(
    has_rule( 'public', 'sometab', 'ins_me', 'whatever' ),
    true,
    'has_rule(schema, table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_rule( 'public', 'sometab', 'ins_me'::name ),
    true,
    'has_rule(schema, table, rule)',
    'Relation public.sometab should have rule ins_me',
    ''
);

SELECT * FROM check_test(
    has_rule( 'public', 'sometab', 'del_me', 'whatever' ),
    false,
    'has_rule(schema, table, rule, desc) fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_rule( 'sometab', 'ins_me', 'whatever' ),
    true,
    'has_rule(table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    has_rule( 'sometab', 'ins_me'::name ),
    true,
    'has_rule(table, rule)',
    'Relation sometab should have rule ins_me',
    ''
);

SELECT * FROM check_test(
    has_rule( 'sometab', 'del_me', 'whatever' ),
    false,
    'has_rule(table, rule, desc) fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'public', 'sometab', 'ins_me', 'whatever' ),
    false,
    'hasnt_rule(schema, table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'public', 'sometab', 'ins_me'::name ),
    false,
    'hasnt_rule(schema, table, rule)',
    'Relation public.sometab should not have rule ins_me',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'public', 'sometab', 'del_me', 'whatever' ),
    true,
    'hasnt_rule(schema, table, rule, desc) fail',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'sometab', 'ins_me', 'whatever' ),
    false,
    'hasnt_rule(table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'sometab', 'ins_me'::name ),
    false,
    'hasnt_rule(table, rule)',
    'Relation sometab should not have rule ins_me',
    ''
);

SELECT * FROM check_test(
    hasnt_rule( 'sometab', 'del_me', 'whatever' ),
    true,
    'hasnt_rule(table, rule, desc) fail',
    'whatever',
    ''
);

/****************************************************************************/
-- Test rule_is_instead().

SELECT * FROM check_test(
    rule_is_instead( 'public', 'sprockets', 'ins_me', 'whatever' ),
    true,
    'rule_is_instead(schema, table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'public', 'sprockets', 'ins_me'::name ),
    true,
    'rule_is_instead(schema, table, rule)',
    'Rule ins_me on relation public.sprockets should be an INSTEAD rule',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'public', 'sometab', 'ins_me', 'whatever' ),
    false,
    'rule_is_instead(schema, table, nothing rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'public', 'sometab', 'upd_me', 'whatever' ),
    false,
    'rule_is_instead(schema, table, also rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'sprockets', 'ins_me', 'whatever' ),
    true,
    'rule_is_instead(table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'sprockets', 'ins_me'::name ),
    true,
    'rule_is_instead(table, rule)',
    'Rule ins_me on relation sprockets should be an INSTEAD rule',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'sometab', 'ins_me', 'whatever' ),
    false,
    'rule_is_instead(table, nothing rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'sometab', 'upd_me', 'whatever' ),
    false,
    'rule_is_instead(table, also rule, desc)',
    'whatever',
    ''
);

-- Check failure diagnostics for non-existent rules.
SELECT * FROM check_test(
    rule_is_instead( 'public', 'sometab', 'blah', 'whatever' ),
    false,
    'rule_is_instead(schema, table, non-existent rule, desc)',
    'whatever',
    '    Rule blah does not exist'
);

SELECT * FROM check_test(
    rule_is_instead( 'sometab', 'blah', 'whatever' ),
    false,
    'rule_is_instead(table, non-existent rule, desc)',
    'whatever',
    '    Rule blah does not exist'
);

/****************************************************************************/
-- Test rule_is_on().
SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'upd_me', 'update', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, update, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sprockets', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'widgets', 'del_me', 'delete', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, delete, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'widgets', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(schema, table, dupe rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me', 'INSERT', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, INSERT, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me', 'i', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, i, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me', 'indigo', 'whatever' ),
    true,
    'rule_is_on(schema, table, rule, indigo, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me'::name, 'i' ),
    true,
    'rule_is_on(schema, table, rule, i, desc)',
    'Rule ins_me should be on INSERT to public.sometab',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'ins_me'::name, 'u' ),
    false,
    'rule_is_on(schema, table, rule, u, desc) fail',
    'Rule ins_me should be on UPDATE to public.sometab',
    '        have: INSERT
        want: UPDATE'
);

SELECT * FROM check_test(
    rule_is_on( 'public', 'sometab', 'foo_me'::name, 'u' ),
    false,
    'rule_is_on(schema, table, invalid rule, u, desc)',
    'Rule foo_me should be on UPDATE to public.sometab',
    '    Rule foo_me does not exist on public.sometab'
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(table, rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'upd_me', 'update', 'whatever' ),
    true,
    'rule_is_on(table, rule, update, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sprockets', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(table, rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'widgets', 'del_me', 'delete', 'whatever' ),
    true,
    'rule_is_on(table, rule, delete, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'widgets', 'ins_me', 'insert', 'whatever' ),
    true,
    'rule_is_on(table, dupe rule, insert, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me', 'INSERT', 'whatever' ),
    true,
    'rule_is_on(table, rule, INSERT, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me', 'i', 'whatever' ),
    true,
    'rule_is_on(table, rule, i, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me', 'indigo', 'whatever' ),
    true,
    'rule_is_on(table, rule, indigo, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me'::name, 'i' ),
    true,
    'rule_is_on(table, rule, i, desc)',
    'Rule ins_me should be on INSERT to sometab',
    ''
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'ins_me'::name, 'u' ),
    false,
    'rule_is_on(table, rule, u, desc) fail',
    'Rule ins_me should be on UPDATE to sometab',
    '        have: INSERT
        want: UPDATE'
);

SELECT * FROM check_test(
    rule_is_on( 'sometab', 'foo_me'::name, 'u' ),
    false,
    'rule_is_on(table, invalid rule, u, desc)',
    'Rule foo_me should be on UPDATE to sometab',
    '    Rule foo_me does not exist on sometab'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
