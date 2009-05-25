\unset ECHO
\i test_setup.sql

SELECT plan(66);
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

CREATE TABLE public.toview ( id INT );
CREATE RULE "_RETURN" AS ON SELECT TO public.toview DO INSTEAD SELECT 42 AS id;

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
    rule_is_instead( 'public', 'toview', '_RETURN', 'whatever' ),
    true,
    'rule_is_instead(schema, table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'public', 'toview', '_RETURN'::name ),
    true,
    'rule_is_instead(schema, table, rule)',
    'Rule "_RETURN" on relation public.toview should be an INSTEAD rule',
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
    rule_is_instead( 'toview', '_RETURN', 'whatever' ),
    true,
    'rule_is_instead(table, rule, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rule_is_instead( 'toview', '_RETURN'::name ),
    true,
    'rule_is_instead(table, rule)',
    'Rule "_RETURN" on relation toview should be an INSTEAD rule',
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
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
