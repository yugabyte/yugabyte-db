\unset ECHO
--\i test/setup.sql

SELECT plan( 14 );

-- Create inherited tables
CREATE TABLE public.parent( id INT PRIMARY KEY );
CREATE TABLE public.child(  id INT PRIMARY KEY ) INHERITS ( public.parent );

-- Create inherited tables in another schema
CREATE SCHEMA hide;
CREATE TABLE hide.h_parent( id INT PRIMARY KEY );
CREATE TABLE hide.h_child(  id INT PRIMARY KEY ) INHERITS ( hide.h_parent );


-- test has_inhereted_tables
SELECT * FROM check_test(
       has_inherited_tables( 'hide'::name, 'h_parent'::name )
       , true  -- expected value
       , 'hide.h_parent is supposed to be parent of other tables'
       );

-- test hasnt_inherited_tables
SELECT * FROM check_test(
       hasnt_inherited_tables( 'hide'::name, 'h_child'::name )
       , true  -- expected value
       , 'hide.h_child is supposed not to have children'
);


-- test has_inhereted_tables
SELECT * FROM check_test(
       has_inherited_tables( 'parent'::name )
       , true  -- expected value
       , 'public.parent is supposed to be parent of other tables'
);

-- test hasnt_inherited_tables
SELECT * FROM check_test(
       hasnt_inherited_tables( 'child'::name )
       , true  -- expected value
       , 'public.child is supposed not to have children'
);




SELECT * FROM check_test(
       is_parent_of( 'hide', 'h_parent', 'hide', 'h_child' )
       , true -- expected value
       , 'hide.h_parent is father of hide.h_child'
);

SELECT * FROM check_test(
       is_parent_of( 'parent', 'child' )
       , true -- expected value
       , 'child inherits from parent'
);


SELECT * FROM check_test(
       is_parent_of( 'hide', 'h_parent', 'public', 'child' )
       , false -- expected value
       , 'hide.h_parent is not father of public.child'
);

SELECT * FROM check_test(
       is_parent_of( 'public', 'parent', 'public', 'child' )
       , true -- expected value
       , 'public.parent is not father of public.child'
);



SELECT * FROM check_test(
       isnt_child_of( 'hide', 'h_parent', 'public', 'child' )
       , true -- expected value
       , 'hide.h_parent is not father of public.child'
);

SELECT * FROM check_test(
       isnt_parent_of( 'parent', 'child' )
       , false -- expected value
       , 'parent is not father of public.child'
);


SELECT * FROM check_test(
       isnt_child_of( 'parent', 'child' )
       , true -- expected value
       , 'parent is not child'
);

SELECT * FROM check_test(
       isnt_child_of( 'child', 'parent' )
       , false -- expected value
       , 'child inherits from parent'
);


SELECT * FROM check_test(
       isnt_child_of( 'hide', 'h_parent', 'hide', 'h_child' )
       , true -- expected value
       , 'hide.h_parent is not child of hide.h_child'
);

SELECT * FROM check_test(
       isnt_child_of( 'hide', 'h_child', 'hide', 'h_parent' )
       , false -- expected value
       , 'hide.h_child inherits from hide.h_parent'
);



/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
