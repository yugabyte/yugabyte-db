\unset ECHO
\i test/setup.sql
SELECT plan( 408 );
-- SELECT * FROM no_plan();
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
    'Table hide.h_parent should have descendents',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'h_child2'::name ),
    false,
    'has_inherited_tables(sch, tab) fail',
    'Table hide.h_child2 should have descendents',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'hide', 'nonesuch'::name ),
    false,
    'has_inherited_tables(sch, nonesuch)',
    'Table hide.nonesuch should have descendents',
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
    'Table parent should have descendents',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'child2' ),
    false,
    'has_inherited_tables(tab) fail',
    'Table child2 should have descendents',
    ''
);

SELECT * FROM check_test(
    has_inherited_tables( 'nonesuch' ),
    false,
    'has_inherited_tables(nonesuch)',
    'Table nonesuch should have descendents',
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
    'Table hide.h_child2 should not have descendents',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'h_child1'::name ),
    false,
    'hasnt_inherited_tables(sch, tab) fail',
    'Table hide.h_child1 should not have descendents',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'hide', 'nonesuch'::name ),
    true,
    'hasnt_inherited_tables(sch, nonesuch)',
    'Table hide.nonesuch should not have descendents',
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
    'Table child2 should not have descendents',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'child1' ),
    false,
    'hasnt_inherited_tables(tab) fail',
    'Table child1 should not have descendents',
    ''
);

SELECT * FROM check_test(
    hasnt_inherited_tables( 'nonesuch' ),
    true,
    'hasnt_inherited_tables(nonesuch)',
    'Table nonesuch should not have descendents',
    ''
);

-- test is_ancestor_of
SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 1, 'Lookie' ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 2, 'Lookie' ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab, 2, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'nope', 'h_child2', 1, 'Lookie' ),
    false,
    'is_ancestor_of(psch, nope, csch, ctab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 1, 'Lookie' ),
    false,
    'is_ancestor_of(psch, ptab, csch, nope, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 1 ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab, 1)',
    'Table hide.h_parent should be ancestor 1 for hide.h_child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'nope', 'hide', 'h_child1', 1 ),
    false,
    'is_ancestor_of(psch, nope, csch, ctab, 1)',
    'Table hide.nope should be ancestor 1 for hide.h_child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 1 ),
    false,
    'is_ancestor_of(psch, ptab, csch, nope, 1)',
    'Table hide.h_parent should be ancestor 1 for hide.nope',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 2 ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab, 2)',
    'Table hide.h_parent should be ancestor 2 for hide.h_child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'nope', 'hide', 'h_child2', 2 ),
    false,
    'is_ancestor_of(psch, nope, csch, ctab, 2)',
    'Table hide.nope should be ancestor 2 for hide.h_child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 2 ),
    false,
    'is_ancestor_of(psch, ptab, csch, nope, 2)',
    'Table hide.h_parent should be ancestor 2 for hide.nope',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 'Howdy' ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 'Howdy' ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1'::name ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab)',
    'Table hide.h_parent should be an ancestor of hide.h_child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2'::name ),
    true,
    'is_ancestor_of(psch, ptab, csch, ctab2)',
    'Table hide.h_parent should be an ancestor of hide.h_child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'nope', 'hide', 'h_child1'::name ),
    false,
    'is_ancestor_of(psch, nope, csch, ctab)',
    'Table hide.nope should be an ancestor of hide.h_child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'hide', 'h_parent', 'hide', 'nope'::name ),
    false,
    'is_ancestor_of(psch, ptab, csch, nope)',
    'Table hide.h_parent should be an ancestor of hide.nope',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child1', 1, 'Howdy' ),
    true,
    'is_ancestor_of(ptab, ctab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child2', 2, 'Howdy' ),
    true,
    'is_ancestor_of(ptab, ctab, 2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child2', 1, 'Howdy' ),
    false,
    'is_ancestor_of(ptab, ctab, 1, desc) fail',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'nope', 1, 'Howdy' ),
    false,
    'is_ancestor_of(ptab, nope, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child1', 1 ),
    true,
    'is_ancestor_of(ptab, ctab, 1)',
    'Table parent should be ancestor 1 of child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child2', 2 ),
    true,
    'is_ancestor_of(ptab, ctab, 2)',
    'Table parent should be ancestor 2 of child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child2', 1 ),
    false,
    'is_ancestor_of(ptab, ctab, 1) fail',
    'Table parent should be ancestor 1 of child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'nope', 1 ),
    false,
    'is_ancestor_of(ptab, nope, 1)',
    'Table parent should be ancestor 1 of nope',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child1' ),
    true,
    'is_ancestor_of(ptab, ctab)',
    'Table parent should be an ancestor of child1',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'child2' ),
    true,
    'is_ancestor_of(ptab, ctab2)',
    'Table parent should be an ancestor of child2',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'parent', 'nope' ),
    false,
    'is_ancestor_of(ptab, nope)',
    'Table parent should be an ancestor of nope',
    ''
);

SELECT * FROM check_test(
    is_ancestor_of( 'nope', 'child1' ),
    false,
    'is_ancestor_of(nope, ctab2)',
    'Table nope should be an ancestor of child1',
    ''
);

-- test isnt_ancestor_of
SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 1, 'Lookie' ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 2, 'Lookie' ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab, 2, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'nope', 'h_child2', 1, 'Lookie' ),
    true,
    'isnt_ancestor_of(psch, nope, csch, ctab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 1, 'Lookie' ),
    true,
    'isnt_ancestor_of(psch, ptab, csch, nope, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 1 ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab, 1)',
    'Table hide.h_parent should not be ancestor 1 for hide.h_child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'nope', 'hide', 'h_child1', 1 ),
    true,
    'isnt_ancestor_of(psch, nope, csch, ctab, 1)',
    'Table hide.nope should not be ancestor 1 for hide.h_child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 1 ),
    true,
    'isnt_ancestor_of(psch, ptab, csch, nope, 1)',
    'Table hide.h_parent should not be ancestor 1 for hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 2 ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab, 2)',
    'Table hide.h_parent should not be ancestor 2 for hide.h_child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'nope', 'hide', 'h_child2', 2 ),
    true,
    'isnt_ancestor_of(psch, nope, csch, ctab, 2)',
    'Table hide.nope should not be ancestor 2 for hide.h_child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'nope', 2 ),
    true,
    'isnt_ancestor_of(psch, ptab, csch, nope, 2)',
    'Table hide.h_parent should not be ancestor 2 for hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1', 'Howdy' ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2', 'Howdy' ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child1'::name ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab)',
    'Table hide.h_parent should not be an ancestor of hide.h_child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'h_child2'::name ),
    false,
    'isnt_ancestor_of(psch, ptab, csch, ctab2)',
    'Table hide.h_parent should not be an ancestor of hide.h_child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'nope', 'hide', 'h_child1'::name ),
    true,
    'isnt_ancestor_of(psch, nope, csch, ctab)',
    'Table hide.nope should not be an ancestor of hide.h_child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'hide', 'h_parent', 'hide', 'nope'::name ),
    true,
    'isnt_ancestor_of(psch, ptab, csch, nope)',
    'Table hide.h_parent should not be an ancestor of hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child1', 1, 'Howdy' ),
    false,
    'isnt_ancestor_of(ptab, ctab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child2', 2, 'Howdy' ),
    false,
    'isnt_ancestor_of(ptab, ctab, 2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child2', 1, 'Howdy' ),
    true,
    'isnt_ancestor_of(ptab, ctab, 1, desc) fail',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'nope', 1, 'Howdy' ),
    true,
    'isnt_ancestor_of(ptab, nope, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child1', 1 ),
    false,
    'isnt_ancestor_of(ptab, ctab, 1)',
    'Table parent should not be ancestor 1 of child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child2', 2 ),
    false,
    'isnt_ancestor_of(ptab, ctab, 2)',
    'Table parent should not be ancestor 2 of child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child2', 1 ),
    true,
    'isnt_ancestor_of(ptab, ctab, 1) fail',
    'Table parent should not be ancestor 1 of child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'nope', 1 ),
    true,
    'isnt_ancestor_of(ptab, nope, 1)',
    'Table parent should not be ancestor 1 of nope',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child1' ),
    false,
    'isnt_ancestor_of(ptab, ctab)',
    'Table parent should not be an ancestor of child1',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'child2' ),
    false,
    'isnt_ancestor_of(ptab, ctab2)',
    'Table parent should not be an ancestor of child2',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'parent', 'nope' ),
    true,
    'isnt_ancestor_of(ptab, nope)',
    'Table parent should not be an ancestor of nope',
    ''
);

SELECT * FROM check_test(
    isnt_ancestor_of( 'nope', 'child1' ),
    true,
    'isnt_ancestor_of(nope, ctab2)',
    'Table nope should not be an ancestor of child1',
    ''
);

-- test is_descendent_of
SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 1, 'Lookie' ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 2, 'Lookie' ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab, 2, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'nope', 'h_child2', 'hide', 'h_parent', 1, 'Lookie' ),
    false,
    'is_descendent_of(csch, ctab, psch, nope, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 1, 'Lookie' ),
    false,
    'is_descendent_of(csch, nope, psch, ptab, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 1 ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab, 1)',
    'Table hide.h_child1 should be descendent 1 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'nope', 1 ),
    false,
    'is_descendent_of(csch, ctab, psch, nope, 1)',
    'Table hide.h_child1 should be descendent 1 from hide.nope',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 1 ),
    false,
    'is_descendent_of(csch, nope, psch, ptab, 1)',
    'Table hide.nope should be descendent 1 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 2 ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab, 2)',
    'Table hide.h_child2 should be descendent 2 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child2', 'hide', 'nope', 2 ),
    false,
    'is_descendent_of(csch, ctab, psch, nope, 2)',
    'Table hide.h_child2 should be descendent 2 from hide.nope',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 2 ),
    false,
    'is_descendent_of(csch, nope, psch, ptab, 2)',
    'Table hide.nope should be descendent 2 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 'Howdy' ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 'Howdy' ),
    true,
    'is_descendent_of(csch, ctab2, psch, ptab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent'::name ),
    true,
    'is_descendent_of(csch, ctab, psch, ptab)',
    'Table hide.h_child1 should be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent'::name ),
    true,
    'is_descendent_of(csch, ctab2, psch, ptab)',
    'Table hide.h_child2 should be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'h_child1', 'hide', 'nope'::name ),
    false,
    'is_descendent_of(csch, ctab, psch, nope)',
    'Table hide.h_child1 should be a descendent of hide.nope',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'hide', 'nope', 'hide', 'h_parent'::name ),
    false,
    'is_descendent_of(csch, nope, psch, ptab)',
    'Table hide.nope should be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child1', 'parent', 1, 'Howdy' ),
    true,
    'is_descendent_of(ctab, ptab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child2', 'parent', 2, 'Howdy' ),
    true,
    'is_descendent_of(ctab, ptab, 2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child2', 'parent', 1, 'Howdy' ),
    false,
    'is_descendent_of(ctab, ptab, 1, desc) fail',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'nope', 'parent', 1, 'Howdy' ),
    false,
    'is_descendent_of(nope, ptab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child1', 'parent', 1 ),
    true,
    'is_descendent_of(ctab, ptab, 1)',
    'Table child1 should be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child2', 'parent', 2 ),
    true,
    'is_descendent_of(ctab, ptab, 2)',
    'Table child2 should be descendent 2 from parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child2', 'parent', 1 ),
    false,
    'is_descendent_of(ctab, ptab, 1) fail',
    'Table child2 should be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'nope', 'parent', 1 ),
    false,
    'is_descendent_of(nope, ptab, 1)',
    'Table nope should be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child1', 'parent' ),
    true,
    'is_descendent_of(ctab, ptab)',
    'Table child1 should be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child2', 'parent' ),
    true,
    'is_descendent_of( ctab2, ptab )',
    'Table child2 should be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'nope', 'parent' ),
    false,
    'is_descendent_of(nope, ptab)',
    'Table nope should be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    is_descendent_of( 'child1', 'nope' ),
    false,
    'is_descendent_of(ctab2, nope)',
    'Table child1 should be a descendent of nope',
    ''
);

-- test isnt_descendent_of
SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 1, 'Lookie' ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 2, 'Lookie' ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab, 2, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'nope', 'h_child2', 'hide', 'h_parent', 1, 'Lookie' ),
    true,
    'isnt_descendent_of(csch, ctab, psch, nope, 1, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 1, 'Lookie' ),
    true,
    'isnt_descendent_of(csch, nope, psch, ptab, desc)',
    'Lookie',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 1 ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab, 1)',
    'Table hide.h_child1 should not be descendent 1 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'nope', 1 ),
    true,
    'isnt_descendent_of(csch, ctab, psch, nope, 1)',
    'Table hide.h_child1 should not be descendent 1 from hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 1 ),
    true,
    'isnt_descendent_of(csch, nope, psch, ptab, 1)',
    'Table hide.nope should not be descendent 1 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 2 ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab, 2)',
    'Table hide.h_child2 should not be descendent 2 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child2', 'hide', 'nope', 2 ),
    true,
    'isnt_descendent_of(csch, ctab, psch, nope, 2)',
    'Table hide.h_child2 should not be descendent 2 from hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'nope', 'hide', 'h_parent', 2 ),
    true,
    'isnt_descendent_of(csch, nope, psch, ptab, 2)',
    'Table hide.nope should not be descendent 2 from hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent', 'Howdy' ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent', 'Howdy' ),
    false,
    'isnt_descendent_of(csch, ctab2, psch, ptab, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'h_parent'::name ),
    false,
    'isnt_descendent_of(csch, ctab, psch, ptab)',
    'Table hide.h_child1 should not be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child2', 'hide', 'h_parent'::name ),
    false,
    'isnt_descendent_of(csch, ctab2, psch, ptab)',
    'Table hide.h_child2 should not be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'h_child1', 'hide', 'nope'::name ),
    true,
    'isnt_descendent_of(csch, ctab, psch, nope)',
    'Table hide.h_child1 should not be a descendent of hide.nope',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'hide', 'nope', 'hide', 'h_parent'::name ),
    true,
    'isnt_descendent_of(csch, nope, psch, ptab)',
    'Table hide.nope should not be a descendent of hide.h_parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child1', 'parent', 1, 'Howdy' ),
    false,
    'isnt_descendent_of(ctab, ptab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child2', 'parent', 2, 'Howdy' ),
    false,
    'isnt_descendent_of(ctab, ptab, 2, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child2', 'parent', 1, 'Howdy' ),
    true,
    'isnt_descendent_of(ctab, ptab, 1, desc) fail',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'nope', 'parent', 1, 'Howdy' ),
    true,
    'isnt_descendent_of(nope, ptab, 1, desc)',
    'Howdy',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child1', 'parent', 1 ),
    false,
    'isnt_descendent_of(ctab, ptab, 1)',
    'Table child1 should not be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child2', 'parent', 2 ),
    false,
    'isnt_descendent_of(ctab, ptab, 2)',
    'Table child2 should not be descendent 2 from parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child2', 'parent', 1 ),
    true,
    'isnt_descendent_of(ctab, ptab, 1) fail',
    'Table child2 should not be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'nope', 'parent', 1 ),
    true,
    'isnt_descendent_of(nope, ptab, 1)',
    'Table nope should not be descendent 1 from parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child1', 'parent' ),
    false,
    'isnt_descendent_of(ctab, ptab)',
    'Table child1 should not be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child2', 'parent' ),
    false,
    'isnt_descendent_of( ctab2, ptab )',
    'Table child2 should not be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'nope', 'parent' ),
    true,
    'isnt_descendent_of(nope, ptab)',
    'Table nope should not be a descendent of parent',
    ''
);

SELECT * FROM check_test(
    isnt_descendent_of( 'child1', 'nope' ),
    true,
    'isnt_descendent_of(ctab2, nope)',
    'Table child1 should not be a descendent of nope',
    ''
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
