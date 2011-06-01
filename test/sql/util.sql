\unset ECHO
\i test/setup.sql

SELECT plan(35);
--SELECT * FROM no_plan();

SELECT is( pg_typeof(42), 'integer', 'pg_type(int) should work' );
SELECT is( pg_typeof(42.1), 'numeric', 'pg_type(numeric) should work' );
SELECT is( pg_typeof(''::text), 'text', 'pg_type(text) should work' );

SELECT is( pg_typeof( pg_version() ), 'text', 'pg_version() should return text' );
SELECT is(
    pg_version(),
    current_setting( 'server_version'),
    'pg_version() should return same as "server_version" setting'
);
SELECT matches(
    pg_version(),
    '^[89][.][[:digit:]]{1,2}([.][[:digit:]]{1,2}|devel|(alpha|beta|rc)[[:digit:]]+)$',
    'pg_version() should work'
);

SELECT CASE WHEN pg_version_num() < 81000
    THEN pass( 'pg_version() should return same as "server_version" setting' )
    ELSE is(
        pg_version_num(),
        current_setting( 'server_version_num')::integer,
        'pg_version() should return same as "server_version" setting'
    )
    END;

SELECT is(
    pg_typeof( pg_version_num() ),
    'integer',
    'pg_version_num() should return integer'
);
SELECT matches(
    pg_version_num()::text,
    '^[89][[:digit:]]{4}$',
    'pg_version_num() should be correct'
);

SELECT matches(
   os_name(),
   '^[[:alnum:]]+$',
   'os_name() should output something like an OS name'
);

SELECT is(
    findfuncs('pg_catalog', '^abs$'),
    ARRAY['pg_catalog.abs'],
    'findfincs() should return distinct values'
);

SELECT matches(
    pgtap_version()::text,
    '^0[.][[:digit:]]{2}$',
    'pgtap_version() should work'
);

/****************************************************************************/
-- Test collect_tap().
SELECT is(
    collect_tap(ARRAY['foo', 'bar', 'baz']),
    'foo
bar
baz',
    'collect_tap(text[]) should simply collect tap'
);

CREATE FUNCTION test_variadic() RETURNS TEXT AS $$
BEGIN
    IF pg_version_num() >= 80400 THEN
        RETURN collect_tap('foo', 'bar', 'baz');
    ELSE
        RETURN collect_tap(ARRAY['foo', 'bar', 'baz']);
    END IF;
END;
$$ LANGUAGE plpgsql;

SELECT is(
    test_variadic(),
    'foo
bar
baz',
    'variadic collect_tap() should simply collect tap'
);

/****************************************************************************/
-- Test display_type().
SELECT is( display_type('int4'::regtype, NULL), 'integer', 'display_type(int4)');
SELECT is( display_type('numeric'::regtype, NULL), 'numeric', 'display_type(numeric)');
SELECT is( display_type('numeric'::regtype, 196612), 'numeric(3,0)', 'display_type(numeric, typmod)');
SELECT is( display_type('"char"'::regtype, NULL), '"char"', 'display_type("char")');
SELECT is( display_type('char'::regtype, NULL), 'character', 'display_type(char)');
SELECT is( display_type('timestamp'::regtype, NULL), 'timestamp without time zone', 'display_type(timestamp)');
SELECT is( display_type('timestamptz'::regtype, NULL), 'timestamp with time zone', 'display_type(timestamptz)');

SELECT is( display_type('foo', 'int4'::regtype, NULL), 'foo.integer', 'display_type(foo, int4)');
SELECT is( display_type('HEY', 'numeric'::regtype, NULL), '"HEY".numeric', 'display_type(HEY, numeric)');
SELECT is( display_type('t z', 'int4'::regtype, NULL), '"t z".integer', 'display_type(t z, int4)');
SELECT is( display_type('text'::regtype, NULL), 'text', 'display type_type(text)');

-- Look at a type not in the current schema.
CREATE SCHEMA __foo;
CREATE DOMAIN __foo.goofy AS text CHECK ( TRUE );
SELECT is( display_type( oid, NULL ), 'goofy', 'display_type(__foo.goofy)' )
  FROM pg_type WHERE typname = 'goofy';

-- Look at types with funny names.
CREATE DOMAIN __foo."this.that" AS text CHECK (TRUE);
SELECT is( display_type( oid, NULL ), '"this.that"', 'display_type(__foo."this.that")' )
  FROM pg_type WHERE typname = 'this.that';

CREATE DOMAIN __foo."this"".that" AS text CHECK (TRUE);
SELECT is( display_type( oid, NULL ), '"this"".that"', 'display_type(__foo."this"".that")' )
  FROM pg_type WHERE typname = 'this".that';

-- Look at types with precision.
CREATE DOMAIN __foo."hey"".yoman" AS numeric CHECK (TRUE);
SELECT is( display_type( oid, 13 ), '"hey"".yoman"(13)', 'display_type(__foo."hey"".yoman", 13)' )
  FROM pg_type WHERE typname = 'hey".yoman';

CREATE DOMAIN "try.this""" AS numeric CHECK (TRUE);
SELECT is( display_type( oid, 42 ), '"try.this"""(42)', 'display_type("try.this""", 42)' )
  FROM pg_type WHERE typname = 'try.this"';

-- Take care about quoting with/without precision
SELECT is(_quote_ident_like('test','public.test'), 'test', 'No quoting is required');
SELECT is(_quote_ident_like('test type','public."test type"'), '"test type"', 'Just quote');
SELECT is(_quote_ident_like('varchar(12)', 'varchar(12)'), 'varchar(12)', 'No quoting is required (with precision)');
SELECT is(_quote_ident_like('test type(123)','myschema."test type"(234)'), '"test type"(123)', 'Quote as type with precision');
SELECT is(_quote_ident_like('test table (123)','public."test table (123)"'), '"test table (123)"', 'Quote as ident without precision');


/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
