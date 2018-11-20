\unset ECHO
\i test/setup.sql
SELECT plan( 85 );
--SELECT * FROM no_plan();
SET client_min_messages = warning;

-- Create inherited tables
CREATE TABLE public.parent( id INT PRIMARY KEY );
CREATE TABLE public.child1( id INT PRIMARY KEY ) INHERITS ( public.parent );
CREATE TABLE public.child2( id INT PRIMARY KEY ) INHERITS ( public.child1 );

-- Create inherited tables in another schema
CREATE SCHEMA hide;
CREATE TABLE hide.h_parent( id INT PRIMARY KEY );
CREATE TABLE hide.h_child1( id INT PRIMARY KEY ) INHERITS ( hide.h_parent );
CREATE TABLE hide.h_child2( id INT PRIMARY KEY ) INHERITS ( hide.h_child1 );

-- test has_inhereted_tables
SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'h_parent', 'Gimme inheritance' ),
    true,
    'has_inherited_tables(sch, tab, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'h_child2', 'Gimme inheritance' ),
    false,
    'has_inherited_tables(sch, tab, desc) fail',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'nonesuch', 'Gimme inheritance' ),
    false,
    'has_inherited_tables(sch, nonesuch, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'h_parent'::name ),
    true,
    'has_inherited_tables(sch, tab)',
    'Table hide.h_parent should have children',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'h_child2'::name ),
    false,
    'has_inherited_tables(sch, tab) fail',
    'Table hide.h_child2 should have children',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'nonesuch'::name ),
    false,
    'has_inherited_tables(sch, nonesuch)',
    'Table hide.nonesuch should have children',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'parent', 'Gimme more' ),
    true,
    'has_inherited_tables(tab, desc)',
    'Gimme more',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'child2', 'Gimme more' ),
    false,
    'has_inherited_tables(tab, desc) fail',
    'Gimme more',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'nonesuch', 'Gimme more' ),
    false,
    'has_inherited_tables(nonesuch, desc)',
    'Gimme more',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'parent' ),
    true,
    'has_inherited_tables(tab)',
    'Table parent should have children',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'child2' ),
    false,
    'has_inherited_tables(tab) fail',
    'Table child2 should have children',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'nonesuch' ),
    false,
    'has_inherited_tables(nonesuch)',
    'Table nonesuch should have children',
    ''
);

-- test hasnt_inherited_tables
SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'h_child2', 'Gimme inheritance' ),
    true,
    'hasnt_inherited_tables(sch, tab, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'h_child1', 'Gimme inheritance' ),
    false,
    'hasnt_inherited_tables(sch, tab, desc) fail',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'nonesuch', 'Gimme inheritance' ),
    true,
    'hasnt_inherited_tables(sch, nonesuch, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'h_child2'::name ),
    true,
    'hasnt_inherited_tables(sch, tab)',
    'Table hide.h_child2 should not have children',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'h_child1'::name ),
    false,
    'hasnt_inherited_tables(sch, tab) fail',
    'Table hide.h_child1 should not have children',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'nonesuch'::name ),
    true,
    'hasnt_inherited_tables(sch, nonesuch)',
    'Table hide.nonesuch should not have children',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'child2', 'Gimme inheritance' ),
    true,
    'hasnt_inherited_tables(tab, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'child1', 'Gimme inheritance' ),
    false,
    'hasnt_inherited_tables(tab, desc) fail',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'nonesuch', 'Gimme inheritance' ),
    true,
    'hasnt_inherited_tables(nonesuch, desc)',
    'Gimme inheritance',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'child2' ),
    true,
    'hasnt_inherited_tables(tab)',
    'Table child2 should not have children',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'child1' ),
    false,
    'hasnt_inherited_tables(tab) fail',
    'Table child1 should not have children',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'nonesuch' ),
    true,
    'hasnt_inherited_tables(nonesuch)',
    'Table nonesuch should not have children',
    ''
);





SELECT * FROM check_test(
    is_parent_of( 'hide', 'h_parent', 'hide', 'h_child1', 1, 'Test hide.h_parent->hide.h_child1' ),
    true, -- expected value
    'hide.h_parent direct is father of hide.h_child1'
);

SELECT * FROM check_test(
    is_parent_of( 'hide', 'h_parent', 'hide', 'h_child1', 1 ),
    true, -- expected value
    'hide.h_parent direct is father of hide.h_child1'
);

SELECT * FROM check_test(
    is_parent_of( 'hide', 'h_child1', 'hide', 'h_child2', 1 ),
    true, -- expected value
    'hide.h_child1 direct is father of hide.h_child2'
);

SELECT * FROM check_test(
    is_parent_of( 'hide', 'h_parent', 'hide', 'h_child2', 2 ),
    true, -- expected value
    'hide.h_parent is father of hide.h_child2'
);

SELECT * FROM check_test(
    is_parent_of( 'parent', 'child1' ),
    true, -- expected value
    'child1 inherits from parent'
);

SELECT * FROM check_test(
    is_parent_of( 'hide'::name, 'h_parent'::name, 'public'::name, 'child1'::name ),
    false, -- expected value
    'hide.h_parent is not father of public.child1'
);

SELECT * FROM check_test(
    is_parent_of( 'public'::name, 'parent'::name, 'public'::name, 'child1'::name ),
    true, -- expected value
    'public.parent is not father of public.child1'
);

SELECT * FROM check_test(
    isnt_child_of( 'hide'::name, 'h_parent'::name, 'public'::name, 'child1'::name ),
    true, -- expected value
    'hide.h_parent is not father of public.child1'
);

SELECT * FROM check_test(
    isnt_parent_of( 'parent', 'child1' ),
    false, -- expected value
    'parent is not father of public.child1'
);

SELECT * FROM check_test(
    isnt_child_of( 'parent', 'child1' ),
    true, -- expected value
    'parent is not child1'
);

SELECT * FROM check_test(
    isnt_child_of( 'child1', 'parent' ),
    false, -- expected value
    'child1 inherits from parent'
);

SELECT * FROM check_test(
    isnt_child_of( 'hide'::name, 'h_parent'::name, 'hide'::name, 'h_child1'::name ),
    true, -- expected value
    'hide.h_parent is not child of hide.h_child1'
);

SELECT * FROM check_test(
    isnt_child_of( 'hide'::name, 'h_child1'::name, 'hide'::name, 'h_parent'::name ),
    false, -- expected value
    'hide.h_child1 inherits from hide.h_parent'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
