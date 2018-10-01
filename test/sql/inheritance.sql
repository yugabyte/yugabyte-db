\unset ECHO
--\i test/setup.sql

SELECT plan( 4 );

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





/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
