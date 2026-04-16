\unset ECHO
\i test/setup.sql

SELECT plan(134);
--SELECT * from no_plan();

-- These will be rolled back. :-)
SET client_min_messages = warning;
CREATE TABLE public.pk (
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
);

CREATE TABLE public.fk (
    id    INT NOT NULL PRIMARY KEY,
    pk_id INT NOT NULL REFERENCES pk(id)
);

CREATE TABLE public.pk2 (
    num int NOT NULL UNIQUE,
    dot int NOT NULL,
    PRIMARY KEY (num, dot)
);

CREATE TABLE public.fk2 (
    pk2_num int NOT NULL,
    pk2_dot int NOT NULL,
    FOREIGN KEY(pk2_num, pk2_dot) REFERENCES pk2(num, dot)
);

CREATE TABLE public.fk3(
    id    INT NOT NULL PRIMARY KEY,
    pk_id INT NOT NULL REFERENCES pk(id),
    pk2_num int NOT NULL,
    pk2_dot int NOT NULL,
    foo_id INT NOT NULL,
    FOREIGN KEY(pk2_num, pk2_dot) REFERENCES pk2( num, dot)
);

CREATE TABLE public.pk3(
    id INT UNIQUE
);

CREATE TABLE public.fk4 (
    id INT REFERENCES pk3(id)
);

CREATE TEMP TABLE temp_pk(
    id    INT NOT NULL PRIMARY KEY,
    name  TEXT DEFAULT ''
);

CREATE TEMP TABLE temp_fk (
    id    INT NOT NULL PRIMARY KEY,
    pk_id INT NOT NULL REFERENCES temp_pk(id)
);

-- Create a funcion to return the temp scheme name.
DO $F$
BEGIN
    IF pg_version_num() >= 95000 THEN
        EXECUTE 'CREATE FUNCTION tmpns() RETURNS NAME AS $$ SELECT pg_my_temp_schema()::regnamespace::name $$ LANGUAGE SQL;';
    ELSE
        EXECUTE 'CREATE FUNCTION tmpns() RETURNS NAME AS $$ SELECT nspname FROM pg_namespace WHERE oid = pg_my_temp_schema() $$ LANGUAGE SQL;';
    END IF;
END;
$F$;

RESET client_min_messages;

/****************************************************************************/
-- Test has_fk().
SELECT * FROM check_test(
    has_fk( 'public', 'fk', 'public.fk should have an fk' ),
    true,
    'has_fk( schema, table, description )',
    'public.fk should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'fk', 'fk should have an fk' ),
    'true',
    'has_fk( table, description )',
    'fk should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'fk4', 'fk4 should have an fk' ),
    'true',
    'has_fk( table4, description )',
    'fk4 should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'public', 'fk4', 'fk4 should have an fk' ),
    'true',
    'has_fk( schema, table4, description )',
    'fk4 should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'fk' ),
    true,
    'has_fk( table )',
    'Table fk should have a foreign key constraint'
);

SELECT * FROM check_test(
    has_fk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should have an fk' ),
    false,
    'has_fk( schema, table, description ) fail',
    'pg_catalog.pg_class should have an fk'
);

SELECT * FROM check_test(
    has_fk( 'pg_class', 'pg_class should have an fk' ),
    false,
    'has_fk( table, description ) fail',
    'pg_class should have an fk'
);

/****************************************************************************/
-- Test hasnt_fk().
SELECT * FROM check_test(
    hasnt_fk( 'public', 'fk', 'public.fk should not have an fk' ),
    false,
    'hasnt_fk( schema, table, description )',
    'public.fk should not have an fk'
);

SELECT * FROM check_test(
    hasnt_fk( 'fk', 'fk should not have an fk' ),
    'false',
    'hasnt_fk( table, description )',
    'fk should not have an fk'
);

SELECT * FROM check_test(
    hasnt_fk( 'fk' ),
    false,
    'hasnt_fk( table )',
    'Table fk should not have a foreign key constraint'
);

SELECT * FROM check_test(
    hasnt_fk( 'pg_catalog', 'pg_class', 'pg_catalog.pg_class should not have an fk' ),
    true,
    'hasnt_fk( schema, table, description ) pass',
    'pg_catalog.pg_class should not have an fk'
);

SELECT * FROM check_test(
    hasnt_fk( 'pg_class', 'pg_class should not have an fk' ),
    true,
    'hasnt_fk( table, description ) pass',
    'pg_class should not have an fk'
);

/****************************************************************************/
-- Test col_is_fk().

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk', 'pk_id', 'public.fk.pk_id should be an fk' ),
    true,
    'col_is_fk( schema, table, column, description )',
    'public.fk.pk_id should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk', 'pk_id', 'fk.pk_id should be an fk' ),
    true,
    'col_is_fk( table, column, description )',
    'fk.pk_id should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk', 'pk_id' ),
    true,
    'col_is_fk( table, column )',
    'Column fk(pk_id) should be a foreign key'
);

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk', 'name', 'public.fk.name should be an fk' ),
    false,
    'col_is_fk( schema, table, column, description )',
    'public.fk.name should be an fk',
    '    Table public.fk has foreign key constraints on these columns:
        pk_id'
);

SELECT * FROM check_test(
    col_is_fk( 'fk3', 'name', 'fk3.name should be an fk' ),
    false,
    'col_is_fk( table, column, description )',
    'fk3.name should be an fk',
    '    Table fk3 has foreign key constraints on these columns:
        pk2_num, pk2_dot
        pk_id'
);

-- Check table with multiple FKs.
SELECT * FROM check_test(
    col_is_fk( 'fk3', 'pk_id' ),
    true,
    'multi-fk col_is_fk test',
    'Column fk3(pk_id) should be a foreign key'
);

-- Check failure for table with no FKs.
SELECT * FROM check_test(
    col_is_fk( 'public', 'pk', 'name', 'pk.name should be an fk' ),
    false,
    'col_is_fk with no FKs',
    'pk.name should be an fk',
    '    Table public.pk has no foreign key columns'
);

SELECT * FROM check_test(
    col_is_fk( 'pk', 'name' ),
    false,
    'col_is_fk with no FKs',
    'Column pk(name) should be a foreign key',
    '    Table pk has no foreign key columns'
);


/****************************************************************************/
-- Test col_is_fk() with an array of columns.

SELECT * FROM check_test(
    col_is_fk( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    true,
    'col_is_fk( schema, table, column[], description )',
    'id + pk2_dot should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should be an fk' ),
    true,
    'col_is_fk( table, column[], description )',
    'id + pk2_dot should be an fk'
);

SELECT * FROM check_test(
    col_is_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'] ),
    true,
    'col_is_fk( table, column[] )',
    'Columns fk2(pk2_num, pk2_dot) should be a foreign key'
);

/****************************************************************************/
-- Test col_isnt_fk().

SELECT * FROM check_test(
    col_isnt_fk( 'public', 'fk', 'pk_id', 'public.fk.pk_id should not be an fk' ),
    false,
    'col_isnt_fk( schema, table, column, description )',
    'public.fk.pk_id should not be an fk',
    ''    
);

SELECT * FROM check_test(
    col_isnt_fk( 'fk', 'pk_id', 'fk.pk_id should not be an fk' ),
    false,
    'col_isnt_fk( table, column, description )',
    'fk.pk_id should not be an fk',
    ''
);

SELECT * FROM check_test(
    col_isnt_fk( 'fk', 'pk_id' ),
    false,
    'col_isnt_fk( table, column )',
    'Column fk(pk_id) should not be a foreign key',
    ''
);

SELECT * FROM check_test(
    col_isnt_fk( 'public', 'fk', 'name', 'public.fk.name should not be an fk' ),
    true,
    'col_isnt_fk( schema, table, column, description )',
    'public.fk.name should not be an fk',
    ''
);

SELECT * FROM check_test(
    col_isnt_fk( 'fk3', 'name', 'fk3.name should not be an fk' ),
    true,
    'col_isnt_fk( table, column, description )',
    'fk3.name should not be an fk',
    ''
);

-- Check table with multiple FKs.
SELECT * FROM check_test(
    col_isnt_fk( 'fk3', 'pk_id' ),
    false,
    'multi-fk col_isnt_fk test',
    'Column fk3(pk_id) should not be a foreign key',
    ''
);

-- Check failure for table with no FKs.
SELECT * FROM check_test(
    col_isnt_fk( 'public', 'pk', 'name', 'pk.name should not be an fk' ),
    true,
    'col_isnt_fk with no FKs',
    'pk.name should not be an fk',
    ''
);

SELECT * FROM check_test(
    col_isnt_fk( 'pk', 'name' ),
    true,
    'col_isnt_fk with no FKs',
    'Column pk(name) should not be a foreign key',
    ''
);

/****************************************************************************/
-- Test col_isnt_fk() with an array of columns.

SELECT * FROM check_test(
    col_isnt_fk( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should not be an fk' ),
    false,
    'col_isnt_fk( schema, table, column[], description )',
    'id + pk2_dot should not be an fk'
);

SELECT * FROM check_test(
    col_isnt_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'id + pk2_dot should not be an fk' ),
    false,
    'col_isnt_fk( table, column[], description )',
    'id + pk2_dot should not be an fk'
);

SELECT * FROM check_test(
    col_isnt_fk( 'fk2', ARRAY['pk2_num', 'pk2_dot'] ),
    false,
    'col_isnt_fk( table, column[] )',
    'Columns fk2(pk2_num, pk2_dot) should not be a foreign key'
);

/****************************************************************************/
-- Test fk_ok().
SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'full fk_ok array',
    'WHATEVER'
);

-- Make sure it works with the temp schema.
SELECT * FROM check_test(
    fk_ok( tmpns(), 'temp_fk', ARRAY['pk_id'], tmpns(), 'temp_pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'pg_my_temp_schema()',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'public', 'pk2', ARRAY['num', 'dot'] ),
    true,
    'multiple fk fk_ok desc',
    'public.fk2(pk2_num, pk2_dot) should reference public.pk2(num, dot)'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['id'] ),
    true,
    'fk_ok array desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'] ),
    true,
    'fk_ok array noschema desc',
    'fk(pk_id) should reference pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk2', ARRAY['pk2_num', 'pk2_dot'], 'pk2', ARRAY['num', 'dot'] ),
    true,
    'multiple fk fk_ok noschema desc',
    'fk2(pk2_num, pk2_dot) should reference pk2(num, dot)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['id'], 'WHATEVER' ),
    true,
    'fk_ok array noschema',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', 'pk_id', 'public', 'pk', 'id' ),
    true,
    'basic fk_ok desc',
    'public.fk(pk_id) should reference public.pk(id)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id', 'WHATEVER' ),
    true,
    'basic fk_ok noschema',
    'WHATEVER'
);

SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id' ),
    true,
    'basic fk_ok noschema desc',
    'fk(pk_id) should reference pk(id)',
    ''
);

-- Make sure check_test() works properly with no name argument.
SELECT * FROM check_test(
    fk_ok( 'fk', 'pk_id', 'pk', 'id' ),
    true
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok fail',
    'WHATEVER',
    '        have: public.fk(pk_id) REFERENCES public.pk(id)
        want: public.fk(pk_id) REFERENCES public.pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'public', 'fk', ARRAY['pk_id'], 'public', 'pk', ARRAY['fid'] ),
    false,
    'fk_ok fail desc',
    'public.fk(pk_id) should reference public.pk(fid)',
    '        have: public.fk(pk_id) REFERENCES public.pk(id)
        want: public.fk(pk_id) REFERENCES public.pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok fail no schema',
    'WHATEVER',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'pk', ARRAY['fid'] ),
    false,
    'fk_ok fail no schema desc',
    'fk(pk_id) should reference pk(fid)',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES pk(fid)'
);

SELECT * FROM check_test(
    fk_ok( 'fk', ARRAY['pk_id'], 'ok', ARRAY['fid'], 'WHATEVER' ),
    false,
    'fk_ok bad PK test',
    'WHATEVER',
    '        have: fk(pk_id) REFERENCES pk(id)
        want: fk(pk_id) REFERENCES ok(fid)'
);

-- Try a table with multiple FKs.
SELECT * FROM check_test(
    fk_ok( 'public', 'fk3', 'pk_id', 'public', 'pk', 'id' ),
    true,
    'double fk schema test',
    'public.fk3(pk_id) should reference public.pk(id)',
    ''
);

SELECT * FROM check_test(
    fk_ok( 'fk3', 'pk_id', 'pk', 'id' ),
    true,
    'double fk test',
    'fk3(pk_id) should reference pk(id)',
    ''
);

-- Try the second FK on that table, which happens to be a multicolumn FK.
SELECT * FROM check_test(
    fk_ok(
        'public', 'fk3', ARRAY['pk2_num', 'pk2_dot'],
        'public', 'pk2', ARRAY['num', 'dot']
    ),
    true,
    'double fk and col schema test',
    'public.fk3(pk2_num, pk2_dot) should reference public.pk2(num, dot)',
    ''
);

-- Try FK columns that reference nothing.
SELECT * FROM check_test(
    fk_ok( 'public', 'fk3', 'id', 'public', 'foo', 'id' ),
    false,
    'missing fk test',
    'public.fk3(id) should reference public.foo(id)',
    '        have: public.fk3(id) REFERENCES NOTHING
        want: public.fk3(id) REFERENCES public.foo(id)'
);

-- Try non-existent FK colums.
SELECT * FROM check_test(
    fk_ok(
        'fk3', ARRAY['pk2_blah', 'pk2_dot'],
        'pk2', ARRAY['num', 'dot']
    ),
    false,
    'bad FK column test',
    'fk3(pk2_blah, pk2_dot) should reference pk2(num, dot)',
    '        have: fk3(pk2_blah, pk2_dot) REFERENCES NOTHING
        want: fk3(pk2_blah, pk2_dot) REFERENCES pk2(num, dot)'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
