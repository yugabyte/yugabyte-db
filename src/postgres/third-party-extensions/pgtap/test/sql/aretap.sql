\unset ECHO
\i test/setup.sql
-- \i sql/pgtap.sql

SELECT plan(459);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

CREATE TABLE public.fou(
    id      INT NOT NULL PRIMARY KEY,
    name    TEXT DEFAULT '',
    numb    NUMERIC(10, 2),
    "myInt" NUMERIC(8)
);
-- Create a partition.
CREATE FUNCTION mkpart() RETURNS SETOF TEXT AS $$
BEGIN
    IF pg_version_num() >= 100000 THEN
        EXECUTE $E$
            CREATE TABLE public.foo (dt DATE NOT NULL) PARTITION BY RANGE (dt);
        $E$;
    ELSE
    EXECUTE $E$
        CREATE TABLE public.foo (dt DATE NOT NULL);
    $E$;
    END IF;
    RETURN;
END;
$$ LANGUAGE plpgsql;
SELECT * FROM mkpart();

CREATE RULE ins_me AS ON INSERT TO public.fou DO NOTHING;
CREATE RULE upd_me AS ON UPDATE TO public.fou DO NOTHING;

CREATE INDEX idx_fou_id ON public.fou(id);
CREATE INDEX idx_fou_name ON public.fou(name);

CREATE VIEW public.voo AS SELECT * FROM foo;
CREATE VIEW public.vou AS SELECT * FROM fou;

CREATE SEQUENCE public.someseq;
CREATE SEQUENCE public.sumeseq;

CREATE SCHEMA someschema;

CREATE FUNCTION someschema.yip() returns boolean as 'SELECT TRUE' language SQL;
CREATE FUNCTION someschema.yap() returns boolean as 'SELECT TRUE' language SQL;

CREATE DOMAIN public.goofy AS text CHECK ( TRUE );
CREATE DOMAIN public."myDomain" AS text CHECK ( TRUE );
CREATE OR REPLACE FUNCTION public.goofy_cmp(goofy,goofy)
RETURNS INTEGER AS $$
    SELECT CASE WHEN $1 = $2 THEN 0
                WHEN $1 > $2 THEN 1
                ELSE -1
    END;
$$ LANGUAGE SQL IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION public.goofy_eq (goofy, goofy) RETURNS boolean AS $$
    SELECT $1 = $2;
$$ LANGUAGE SQL IMMUTABLE STRICT;

CREATE SCHEMA disney;

CREATE OPERATOR disney.= ( PROCEDURE = goofy_eq, LEFTARG = goofy, RIGHTARG = goofy);
CREATE OPERATOR = ( PROCEDURE = goofy_eq, LEFTARG = goofy, RIGHTARG = goofy);

CREATE OPERATOR CLASS public.goofy_ops
DEFAULT FOR TYPE goofy USING BTREE AS
	OPERATOR 1 disney.=,
	FUNCTION 1 goofy_cmp(goofy,goofy)
;

CREATE TYPE someschema.sometype AS (
    id    INT,
    name  TEXT
);

CREATE TYPE someschema."myType" AS (
    id INT,
    foo INT
);

-- Create a procedure.
DO $$
BEGIN
    IF pg_version_num() >= 110000 THEN
        EXECUTE 'CREATE PROCEDURE someschema.someproc(int) LANGUAGE SQL AS ''''';
    ELSE
        CREATE FUNCTION someschema.someproc(int)
        RETURNS void AS ''
        LANGUAGE SQL;
    END IF;
END;
$$;

RESET client_min_messages;

/****************************************************************************/
-- Test tablespaces_are().

CREATE FUNCTION ___myts(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT spcname
          FROM pg_catalog.pg_tablespace
         WHERE spcname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    tablespaces_are( ___myts(''), 'whatever' ),
    true,
    'tablespaces_are(tablespaces, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tablespaces_are( ___myts('') ),
    true,
    'tablespaces_are(tablespaces)',
    'There should be the correct tablespaces',
    ''
);

SELECT * FROM check_test(
    tablespaces_are( array_append(___myts(''), '__booya__'), 'whatever' ),
    false,
    'tablespaces_are(tablespaces, desc) missing',
    'whatever',
    '    Missing tablespaces:
        __booya__'
);

SELECT * FROM check_test(
    tablespaces_are( ___myts('pg_default'), 'whatever' ),
    false,
    'tablespaces_are(tablespaces, desc) extra',
    'whatever',
    '    Extra tablespaces:
        pg_default'
);

SELECT * FROM check_test(
    tablespaces_are( array_append(___myts('pg_default'), '__booya__'), 'whatever' ),
    false,
    'tablespaces_are(tablespaces, desc) extras and missing',
    'whatever',
    '    Extra tablespaces:
        pg_default
    Missing tablespaces:
        __booya__'
);

/****************************************************************************/
-- Test schemas_are().

CREATE FUNCTION ___mysch(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT nspname
          FROM pg_catalog.pg_namespace
         WHERE nspname NOT LIKE 'pg_%'
           AND nspname <> 'information_schema'
           AND nspname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    schemas_are( ___mysch(''), 'whatever' ),
    true,
    'schemas_are(schemas, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    schemas_are( ___mysch('') ),
    true,
    'schemas_are(schemas)',
    'There should be the correct schemas',
    ''
);

SELECT * FROM check_test(
    schemas_are( array_append(___mysch(''), '__howdy__'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) missing',
    'whatever',
    '    Missing schemas:
        __howdy__'
);

SELECT * FROM check_test(
    schemas_are( ___mysch('someschema'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) extras',
    'whatever',
    '    Extra schemas:
        someschema'
);

SELECT * FROM check_test(
    schemas_are( array_append(___mysch('someschema'), '__howdy__'), 'whatever' ),
    false,
    'schemas_are(schemas, desc) missing and extras',
    'whatever',
    '    Extra schemas:
        someschema
    Missing schemas:
        __howdy__'
);

/****************************************************************************/
-- Test tables_are().

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo'], 'whatever' ),
    true,
    'tables_are(schema, tables, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo'] ),
    true,
    'tables_are(schema, tables)',
    'Schema public should have the correct tables',
    ''
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo'] ),
    true,
    'tables_are(tables)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    ''
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo'], 'whatever' ),
    true,
    'tables_are(tables, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou', 'foo', 'bar'] ),
    false,
    'tables_are(schema, tables) missing',
    'Schema public should have the correct tables',
    '    Missing tables:
        bar'
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou', 'foo', 'bar'] ),
    false,
    'tables_are(tables) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Missing tables:
        bar'
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['fou'] ),
    false,
    'tables_are(schema, tables) extra',
    'Schema public should have the correct tables',
    '    Extra tables:
        foo'
);

SELECT * FROM check_test(
    tables_are( ARRAY['fou'] ),
    false,
    'tables_are(tables) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Extra tables:
        foo'
);

SELECT * FROM check_test(
    tables_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'tables_are(schema, tables) extra and missing',
    'Schema public should have the correct tables',
    '    Extra tables:
        fo[ou]
        fo[ou]
    Missing tables:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    tables_are( ARRAY['bar', 'baz'] ),
    false,
    'tables_are(tables) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct tables',
    '    Extra tables:' || '
        fo[ou]
        fo[ou]
    Missing tables:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test views_are().
SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo'], 'whatever' ),
    true,
    'views_are(schema, views, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo'] ),
    true,
    'views_are(schema, views)',
    'Schema public should have the correct views',
    ''
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo'] ),
    true,
    'views_are(views)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    ''
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo'], 'whatever' ),
    true,
    'views_are(views, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou', 'voo', 'bar'] ),
    false,
    'views_are(schema, views) missing',
    'Schema public should have the correct views',
    '    Missing views:
        bar'
);

SELECT * FROM check_test(
    views_are( ARRAY['vou', 'voo', 'bar'] ),
    false,
    'views_are(views) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Missing views:
        bar'
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['vou'] ),
    false,
    'views_are(schema, views) extra',
    'Schema public should have the correct views',
    '    Extra views:
        voo'
);

SELECT * FROM check_test(
    views_are( ARRAY['vou'] ),
    false,
    'views_are(views) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Extra views:
        voo'
);

SELECT * FROM check_test(
    views_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'views_are(schema, views) extra and missing',
    'Schema public should have the correct views',
    '    Extra views:
        vo[ou]
        vo[ou]
    Missing views:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    views_are( ARRAY['bar', 'baz'] ),
    false,
    'views_are(views) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
    '    Extra views:' || '
        vo[ou]
        vo[ou]
    Missing views:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test sequences_are().
SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq'], 'whatever' ),
    true,
    'sequences_are(schema, sequences, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq'] ),
    true,
    'sequences_are(schema, sequences)',
    'Schema public should have the correct sequences',
    ''
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq'] ),
    true,
    'sequences_are(sequences)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    ''
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq'], 'whatever' ),
    true,
    'sequences_are(sequences, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq', 'someseq', 'bar'] ),
    false,
    'sequences_are(schema, sequences) missing',
    'Schema public should have the correct sequences',
    '    Missing sequences:
        bar'
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq', 'someseq', 'bar'] ),
    false,
    'sequences_are(sequences) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Missing sequences:
        bar'
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['sumeseq'] ),
    false,
    'sequences_are(schema, sequences) extra',
    'Schema public should have the correct sequences',
    '    Extra sequences:
        someseq'
);

SELECT * FROM check_test(
    sequences_are( ARRAY['sumeseq'] ),
    false,
    'sequences_are(sequences) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Extra sequences:
        someseq'
);

SELECT * FROM check_test(
    sequences_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'sequences_are(schema, sequences) extra and missing',
    'Schema public should have the correct sequences',
    '    Extra sequences:
        s[ou]meseq
        s[ou]meseq
    Missing sequences:
        ba[rz]
        ba[rz]',
    true
);

SELECT * FROM check_test(
    sequences_are( ARRAY['bar', 'baz'] ),
    false,
    'sequences_are(sequences) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct sequences',
    '    Extra sequences:' || '
        s[ou]meseq
        s[ou]meseq
    Missing sequences:' || '
        ba[rz]
        ba[rz]',
    true
);

/****************************************************************************/
-- Test functions_are().

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap', 'someproc'], 'whatever' ),
    true,
    'functions_are(schema, functions, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap', 'someproc'] ),
    true,
    'functions_are(schema, functions)',
    'Schema someschema should have the correct functions'
    ''
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'yap', 'someproc', 'yop'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + missing',
    'whatever',
    '    Missing functions:
        yop'
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yip', 'someproc'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + extra',
    'whatever',
    '    Extra functions:
        yap'
);

SELECT * FROM check_test(
    functions_are( 'someschema', ARRAY['yap', 'yop', 'someproc'], 'whatever' ),
    false,
    'functions_are(schema, functions, desc) + extra & missing',
    'whatever',
    '    Extra functions:
        yip
    Missing functions:
        yop'
);

CREATE FUNCTION ___myfunk(ex text) RETURNS NAME[] AS $$
    SELECT ARRAY(
        SELECT p.proname
          FROM pg_catalog.pg_namespace n
          JOIN pg_catalog.pg_proc p ON n.oid = p.pronamespace
         WHERE pg_catalog.pg_function_is_visible(p.oid)
           AND n.nspname NOT IN ('pg_catalog', 'information_schema')
           AND p.proname <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    functions_are( ___myfunk(''), 'whatever' ),
    true,
    'functions_are(functions, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    functions_are( ___myfunk('') ),
    true,
    'functions_are(functions)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct functions',
    ''
);

SELECT * FROM check_test(
    functions_are( array_append(___myfunk(''), '__booyah__'), 'whatever' ),
    false,
    'functions_are(functions, desc) + missing',
    'whatever',
    '    Missing functions:
        __booyah__'
);

SELECT * FROM check_test(
    functions_are( ___myfunk('check_test'), 'whatever' ),
    false,
    'functions_are(functions, desc) + extra',
    'whatever',
    '    Extra functions:
        check_test'
);

SELECT * FROM check_test(
    functions_are( array_append(___myfunk('check_test'), '__booyah__'), 'whatever' ),
    false,
    'functions_are(functions, desc) + extra & missing',
    'whatever',
    '    Extra functions:
        check_test
    Missing functions:
        __booyah__'
);

/****************************************************************************/
-- Test indexes_are().
SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'], 'whatever' ),
    true,
    'indexes_are(schema, table, indexes, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'] ),
    true,
    'indexes_are(schema, table, indexes)',
    'Table public.fou should have the correct indexes',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name'] ),
    false,
    'indexes_are(schema, table, indexes) + extra',
    'Table public.fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey'
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey', 'howdy'] ),
    false,
    'indexes_are(schema, table, indexes) + missing',
    'Table public.fou should have the correct indexes',
    '    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'public', 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'howdy'] ),
    false,
    'indexes_are(schema, table, indexes) + extra & missing',
    'Table public.fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey
    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'], 'whatever' ),
    true,
    'indexes_are(table, indexes, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey'] ),
    true,
    'indexes_are(table, indexes)',
    'Table fou should have the correct indexes',
    ''
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name'] ),
    false,
    'indexes_are(table, indexes) + extra',
    'Table fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'fou_pkey', 'howdy'] ),
    false,
    'indexes_are(table, indexes) + missing',
    'Table fou should have the correct indexes',
    '    Missing indexes:
        howdy'
);

SELECT * FROM check_test(
    indexes_are( 'fou', ARRAY['idx_fou_id', 'idx_fou_name', 'howdy'] ),
    false,
    'indexes_are(table, indexes) + extra & missing',
    'Table fou should have the correct indexes',
    '    Extra indexes:
        fou_pkey
    Missing indexes:
        howdy'
);

/****************************************************************************/
-- Test users_are().

CREATE FUNCTION ___myusers(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY( SELECT usename FROM pg_catalog.pg_user WHERE usename <> $1 ), '{}'::name[]);;
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    users_are( ___myusers(''), 'whatever' ),
    true,
    'users_are(users, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    users_are( ___myusers('') ),
    true,
    'users_are(users)',
    'There should be the correct users',
    ''
);

SELECT * FROM check_test(
    users_are( array_append(___myusers(''), '__howdy__'), 'whatever' ),
    false,
    'users_are(users, desc) missing',
    'whatever',
    '    Missing users:
        __howdy__'
);

SELECT * FROM check_test(
    users_are( ___myusers(current_user), 'whatever' ),
    false,
    'users_are(users, desc) extras',
    'whatever',
    '    Extra users:
        ' || quote_ident(current_user)
);

SELECT * FROM check_test(
    users_are( array_append(___myusers(current_user), '__howdy__'), 'whatever' ),
    false,
    'users_are(users, desc) missing and extras',
    'whatever',
    '    Extra users:
        ' || quote_ident(current_user) || '
    Missing users:
        __howdy__'
);

/****************************************************************************/
-- Test groups_are().

CREATE GROUP meanies;
CREATE FUNCTION ___mygroups(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY( SELECT groname FROM pg_catalog.pg_group WHERE groname <> $1 ), '{}'::name[]);;
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    groups_are( ___mygroups(''), 'whatever' ),
    true,
    'groups_are(groups, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    groups_are( ___mygroups('') ),
    true,
    'groups_are(groups)',
    'There should be the correct groups',
    ''
);

SELECT * FROM check_test(
    groups_are( array_append(___mygroups(''), '__howdy__'), 'whatever' ),
    false,
    'groups_are(groups, desc) missing',
    'whatever',
    '    Missing groups:
        __howdy__'
);

SELECT * FROM check_test(
    groups_are( ___mygroups('meanies'), 'whatever' ),
    false,
    'groups_are(groups, desc) extras',
    'whatever',
    '    Extra groups:
        meanies'
);

SELECT * FROM check_test(
    groups_are( array_append(___mygroups('meanies'), '__howdy__'), 'whatever' ),
    false,
    'groups_are(groups, desc) missing and extras',
    'whatever',
    '    Extra groups:
        meanies
    Missing groups:
        __howdy__'
);

/****************************************************************************/
-- Test languages_are().

CREATE FUNCTION ___mylangs(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY( SELECT lanname FROM pg_catalog.pg_language WHERE lanispl AND lanname <> $1 ), '{}'::name[]);;
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    languages_are( ___mylangs(''), 'whatever' ),
    true,
    'languages_are(languages, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    languages_are( ___mylangs('') ),
    true,
    'languages_are(languages)',
    'There should be the correct procedural languages',
    ''
);

SELECT * FROM check_test(
    languages_are( array_append(___mylangs(''), 'plomgwtf'), 'whatever' ),
    false,
    'languages_are(languages, desc) missing',
    'whatever',
    '    Missing languages:
        plomgwtf'
);

SELECT * FROM check_test(
    languages_are( ___mylangs('plpgsql'), 'whatever' ),
    false,
    'languages_are(languages, desc) extras',
    'whatever',
    '    Extra languages:
        plpgsql'
);

SELECT * FROM check_test(
    languages_are( array_append(___mylangs('plpgsql'), 'plomgwtf'), 'whatever' ),
    false,
    'languages_are(languages, desc) missing and extras',
    'whatever',
    '    Extra languages:
        plpgsql
    Missing languages:
        plomgwtf'
);

/****************************************************************************/
-- Test opclasses_are().

SELECT * FROM check_test(
    opclasses_are( 'public', ARRAY['goofy_ops'], 'whatever' ),
    true,
    'opclasses_are(schema, opclasses, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    opclasses_are( 'public', ARRAY['goofy_ops'] ),
    true,
    'opclasses_are(schema, opclasses)',
    'Schema public should have the correct operator classes'
    ''
);

SELECT * FROM check_test(
    opclasses_are( 'public', ARRAY['goofy_ops', 'yops'], 'whatever' ),
    false,
    'opclasses_are(schema, opclasses, desc) + missing',
    'whatever',
    '    Missing operator classes:
        yops'
);

SELECT * FROM check_test(
    opclasses_are( 'public', '{}'::name[], 'whatever' ),
    false,
    'opclasses_are(schema, opclasses, desc) + extra',
    'whatever',
    '    Extra operator classes:
        goofy_ops'
);

SELECT * FROM check_test(
    opclasses_are( 'public', ARRAY['yops'], 'whatever' ),
    false,
    'opclasses_are(schema, opclasses, desc) + extra & missing',
    'whatever',
    '    Extra operator classes:
        goofy_ops
    Missing operator classes:
        yops'
);

CREATE FUNCTION ___myopc(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY(
        SELECT oc.opcname
          FROM pg_catalog.pg_opclass oc
          JOIN pg_catalog.pg_namespace n ON oc.opcnamespace = n.oid
         WHERE n.nspname <> 'pg_catalog'
           AND oc.opcname <> $1
           AND pg_catalog.pg_opclass_is_visible(oc.oid)
    ), '{}'::name[]);;
$$ LANGUAGE SQL;


SELECT * FROM check_test(
    opclasses_are( ___myopc('') ),
    true,
    'opclasses_are(opclasses)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct operator classes',
    ''
);

SELECT * FROM check_test(
    opclasses_are( array_append(___myopc(''), 'sillyops'), 'whatever' ),
    false,
    'opclasses_are(opclasses, desc) + missing',
    'whatever',
    '    Missing operator classes:
        sillyops'
);

SELECT * FROM check_test(
    opclasses_are( ___myopc('goofy_ops'), 'whatever' ),
    false,
    'opclasses_are(opclasses, desc) + extra',
    'whatever',
    '    Extra operator classes:
        goofy_ops'
);

SELECT * FROM check_test(
    opclasses_are( array_append(___myopc('goofy_ops'), 'sillyops'), 'whatever' ),
    false,
    'opclasses_are(opclasses, desc) + extra & missing',
    'whatever',
    '    Extra operator classes:
        goofy_ops
    Missing operator classes:
        sillyops'
);

/****************************************************************************/
-- Test rules_are().
SELECT * FROM check_test(
    rules_are( 'public', 'fou', ARRAY['ins_me', 'upd_me'], 'whatever' ),
    true,
    'rules_are(schema, table, rules, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rules_are( 'public', 'fou', ARRAY['ins_me', 'upd_me'] ),
    true,
    'rules_are(schema, table, rules)',
    'Relation public.fou should have the correct rules',
    ''
);

SELECT * FROM check_test(
    rules_are( 'public', 'fou', ARRAY['ins_me'] ),
    false,
    'rules_are(schema, table, rules) + extra',
    'Relation public.fou should have the correct rules',
    '    Extra rules:
        upd_me'
);

SELECT * FROM check_test(
    rules_are( 'public', 'fou', ARRAY['ins_me', 'upd_me', 'del_me'] ),
    false,
    'rules_are(schema, table, rules) + missing',
    'Relation public.fou should have the correct rules',
    '    Missing rules:
        del_me'
);

SELECT * FROM check_test(
    rules_are( 'public', 'fou', ARRAY['ins_me', 'del_me'] ),
    false,
    'rules_are(schema, table, rules) + extra & missing',
    'Relation public.fou should have the correct rules',
    '    Extra rules:
        upd_me
    Missing rules:
        del_me'
);

SELECT * FROM check_test(
    rules_are( 'fou', ARRAY['ins_me', 'upd_me'], 'whatever' ),
    true,
    'rules_are(table, rules, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    rules_are( 'fou', ARRAY['ins_me', 'upd_me'] ),
    true,
    'rules_are(table, rules)',
    'Relation fou should have the correct rules',
    ''
);

SELECT * FROM check_test(
    rules_are( 'fou', ARRAY['ins_me'] ),
    false,
    'rules_are(table, rules) + extra',
    'Relation fou should have the correct rules',
    '    Extra rules:
        upd_me'
);

SELECT * FROM check_test(
    rules_are( 'fou', ARRAY['ins_me', 'upd_me', 'del_me'] ),
    false,
    'rules_are(table, rules) + missing',
    'Relation fou should have the correct rules',
    '    Missing rules:
        del_me'
);

SELECT * FROM check_test(
    rules_are( 'fou', ARRAY['ins_me', 'del_me'] ),
    false,
    'rules_are(table, rules) + extra & missing',
    'Relation fou should have the correct rules',
    '    Extra rules:
        upd_me
    Missing rules:
        del_me'
);

/****************************************************************************/
-- Test types_are().

SELECT * FROM check_test(
    types_are( 'someschema', ARRAY['sometype', 'myType'], 'whatever' ),
    true,
    'types_are(schema, types, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    types_are( 'someschema', ARRAY['sometype', 'myType'] ),
    true,
    'types_are(schema, types)',
    'Schema someschema should have the correct types'
    ''
);

SELECT * FROM check_test(
    types_are( 'someschema', ARRAY['sometype', 'myType', 'typo'], 'whatever' ),
    false,
    'types_are(schema, types, desc) + missing',
    'whatever',
    '    Missing types:
        typo'
);

SELECT * FROM check_test(
    types_are( 'someschema', ARRAY['myType'], 'whatever' ),
    false,
    'types_are(schema, types, desc) + extra',
    'whatever',
    '    Extra types:
        sometype'
);

SELECT * FROM check_test(
    types_are( 'someschema', ARRAY['typo', 'myType'], 'whatever' ),
    false,
    'types_are(schema, types, desc) + extra & missing',
    'whatever',
    '    Extra types:
        sometype
    Missing types:
        typo'
);

CREATE FUNCTION ___mytype(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY(
            SELECT t.typname
              FROM pg_catalog.pg_type t
              LEFT JOIN pg_catalog.pg_namespace n ON n.oid = t.typnamespace
             WHERE (
                     t.typrelid = 0
                 OR (SELECT c.relkind = 'c' FROM pg_catalog.pg_class c WHERE c.oid = t.typrelid)
             )
               AND NOT EXISTS(SELECT 1 FROM pg_catalog.pg_type el WHERE el.oid = t.typelem)
               AND n.nspname NOT IN('pg_catalog', 'information_schema')
           AND t.typname <> $1
           AND pg_catalog.pg_type_is_visible(t.oid)
    ), '{}'::name[]);;
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    types_are( ___mytype('') ),
    true,
    'types_are(types)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct types',
    ''
);

SELECT * FROM check_test(
    types_are( array_append(___mytype(''), 'silltypo'), 'whatever' ),
    false,
    'types_are(types, desc) + missing',
    'whatever',
    '    Missing types:
        silltypo'
);

SELECT * FROM check_test(
    types_are( ___mytype('goofy'), 'whatever' ),
    false,
    'types_are(types, desc) + extra',
    'whatever',
    '    Extra types:
        goofy'
);

SELECT * FROM check_test(
    types_are( array_append(___mytype('goofy'), 'silltypo'), 'whatever' ),
    false,
    'types_are(types, desc) + extra & missing',
    'whatever',
    '    Extra types:
        goofy
    Missing types:
        silltypo'
);

/****************************************************************************/
-- Test domains_are().

SELECT * FROM check_test(
    domains_are( 'public', ARRAY['goofy', 'myDomain'], 'whatever' ),
    true,
    'domains_are(schema, domains, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domains_are( 'public', ARRAY['goofy', 'myDomain'] ),
    true,
    'domains_are(schema, domains)',
    'Schema public should have the correct domains',
    ''
);

SELECT * FROM check_test(
    domains_are( 'public', ARRAY['freddy', 'myDomain'], 'whatever' ),
    false,
    'domains_are(schema, domains, desc) fail',
    'whatever',
    '    Extra types:
        goofy
    Missing types:
        freddy'
);

SELECT * FROM check_test(
    domains_are( 'public', ARRAY['freddy', 'myDomain'] ),
    false,
    'domains_are(schema, domains) fail',
    'Schema public should have the correct domains',
    '    Extra types:
        goofy
    Missing types:
        freddy'
);

CREATE FUNCTION ___mydo(ex text) RETURNS NAME[] AS $$
    SELECT COALESCE(ARRAY(
            SELECT t.typname
              FROM pg_catalog.pg_type t
              LEFT JOIN pg_catalog.pg_namespace n ON n.oid = t.typnamespace
             WHERE (
                     t.typrelid = 0
                 OR (SELECT c.relkind = 'c' FROM pg_catalog.pg_class c WHERE c.oid = t.typrelid)
             )
               AND NOT EXISTS(SELECT 1 FROM pg_catalog.pg_type el WHERE el.oid = t.typelem)
               AND n.nspname NOT IN('pg_catalog', 'information_schema')
           AND t.typname <> $1
           AND pg_catalog.pg_type_is_visible(t.oid)
           AND t.typtype = 'd'
    ), '{}'::name[]);
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    domains_are( ___mydo(''), 'whatever' ),
    true,
    'domains_are(domains, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    domains_are( ___mydo('') ),
    true,
    'domains_are(domains)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct domains',
    ''
);

SELECT * FROM check_test(
    domains_are( array_append(___mydo('goofy'), 'fredy'), 'whatever' ),
    false,
    'domains_are(domains, desc) fail',
    'whatever',
    '    Extra types:
        goofy
    Missing types:
        fredy'
);

SELECT * FROM check_test(
    domains_are( array_append(___mydo('goofy'), 'fredy') ),
    false,
    'domains_are(domains) fail',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct domains',
    '    Extra types:
        goofy
    Missing types:
        fredy'
);

/****************************************************************************/
-- Test casts_are().

CREATE OR REPLACE FUNCTION ___mycasts(ex text) RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT pg_catalog.format_type(castsource, NULL)
               || ' AS ' || pg_catalog.format_type(casttarget, NULL)
          FROM pg_catalog.pg_cast
         WHERE castsource::regtype::text <> $1
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    casts_are( ___mycasts(''), 'whatever' ),
    true,
    'casts_are(casts, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    casts_are( ___mycasts('') ),
    true,
    'casts_are(casts)',
    'There should be the correct casts',
    ''
);

SELECT * FROM check_test(
    casts_are( array_append(___mycasts(''), '__booya__ AS integer'), 'whatever' ),
    false,
    'casts_are(casts, desc) missing',
    'whatever',
    '    Missing casts:
        __booya__ AS integer'
);

SELECT * FROM check_test(
    casts_are( ___mycasts('lseg'), 'whatever' ),
    false,
    'casts_are(casts, desc) extra',
    'whatever',
    '    Extra casts:
        lseg AS point'
);

SELECT * FROM check_test(
    casts_are( array_append(___mycasts('lseg'), '__booya__ AS integer'), 'whatever' ),
    false,
    'casts_are(casts, desc) extras and missing',
    'whatever',
    '    Extra casts:
        lseg AS point
    Missing casts:
        __booya__ AS integer'
);

/****************************************************************************/
-- Test operators_are().
--SET search_path = disney,public,pg_catalog;

SELECT * FROM check_test(
    operators_are( 'disney', ARRAY['=(goofy,goofy) RETURNS boolean'], 'whatever' ),
    true,
    'operators_are(schema, operators, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    operators_are( 'disney', ARRAY['=(goofy,goofy) RETURNS boolean'] ),
    true,
    'operators_are(schema, operators)',
    'Schema disney should have the correct operators',
    ''
);

SELECT * FROM check_test(
    operators_are( 'disney', ARRAY['+(freddy,freddy) RETURNS barnie'], 'whatever' ),
    false,
    'operators_are(schema, operators, desc) fail',
    'whatever',
    '    Extra operators:
        =(goofy,goofy) RETURNS boolean
    Missing operators:
        +(freddy,freddy) RETURNS barnie'
);

SELECT * FROM check_test(
    operators_are( 'disney', ARRAY['+(freddy,freddy) RETURNS barnie'] ),
    false,
    'operators_are(schema, operators) fail',
    'Schema disney should have the correct operators',
    '    Extra operators:
        =(goofy,goofy) RETURNS boolean
    Missing operators:
        +(freddy,freddy) RETURNS barnie'
);

CREATE OR REPLACE FUNCTION ___myops(ex text) RETURNS TEXT[] AS $$
    SELECT ARRAY(
        SELECT textin(regoperatorout(o.oid::regoperator)) || ' RETURNS ' || o.oprresult::regtype::text
          FROM pg_catalog.pg_operator o
          JOIN pg_catalog.pg_namespace n ON o.oprnamespace = n.oid
         WHERE pg_catalog.pg_operator_is_visible(o.oid)
           AND o.oprleft::regtype::text <> $1
           AND n.nspname NOT IN ('pg_catalog', 'information_schema')
    );
$$ LANGUAGE SQL;

SELECT * FROM check_test(
    operators_are( array_append( ___myops(''), '=(goofy,goofy) RETURNS boolean' ), 'whatever' ),
    true,
    'operators_are(operators, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    operators_are( array_append( ___myops(''), '=(goofy,goofy) RETURNS boolean' ) ),
    true,
    'operators_are(operators)',
    'There should be the correct operators',
    ''
);

SELECT * FROM check_test(
    operators_are( array_append( ___myops('goofy'), '+(freddy,freddy) RETURNS barnie' ), 'whatever' ),
    false,
    'operators_are(operators, desc) fail',
    'whatever',
    '    Extra operators:
        =(goofy,goofy) RETURNS boolean
    Missing operators:
        +(freddy,freddy) RETURNS barnie'
);

SELECT * FROM check_test(
    operators_are( array_append( ___myops('goofy'), '+(freddy,freddy) RETURNS barnie' ) ),
    false,
    'operators_are(operators) fail',
    'There should be the correct operators',
    '    Extra operators:
        =(goofy,goofy) RETURNS boolean
    Missing operators:
        +(freddy,freddy) RETURNS barnie'
);

/****************************************************************************/
-- Test columns_are().
SELECT * FROM check_test(
    columns_are( 'public', 'fou', ARRAY['id', 'name', 'numb', 'myInt'], 'whatever' ),
    true,
    'columns_are(schema, table, columns, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    columns_are( 'public', 'fou', ARRAY['id', 'name', 'numb', 'myInt'] ),
    true,
    'columns_are(schema, table, columns)',
    'Table public.fou should have the correct columns',
    ''
);

SELECT * FROM check_test(
    columns_are( 'public', 'fou', ARRAY['id', 'name', 'numb'] ),
    false,
    'columns_are(schema, table, columns) + extra',
    'Table public.fou should have the correct columns',
    '    Extra columns:
        "myInt"'
);

SELECT * FROM check_test(
    columns_are( 'public', 'fou', ARRAY['id', 'name', 'numb', 'myInt', 'howdy'] ),
    false,
    'columns_are(schema, table, columns) + missing',
    'Table public.fou should have the correct columns',
    '    Missing columns:
        howdy'
);

SELECT * FROM check_test(
    columns_are( 'public', 'fou', ARRAY['id', 'name', 'numb', 'howdy'] ),
    false,
    'columns_are(schema, table, columns) + extra & missing',
    'Table public.fou should have the correct columns',
    '    Extra columns:
        "myInt"
    Missing columns:
        howdy'
);

SELECT * FROM check_test(
    columns_are( 'fou', ARRAY['id', 'name', 'numb', 'myInt'], 'whatever' ),
    true,
    'columns_are(table, columns, desc)',
    'whatever',
    ''
);

SELECT * FROM check_test(
    columns_are( 'fou', ARRAY['id', 'name', 'numb', 'myInt'] ),
    true,
    'columns_are(table, columns)',
    'Table fou should have the correct columns',
    ''
);

SELECT * FROM check_test(
    columns_are( 'fou', ARRAY['id', 'name', 'numb'] ),
    false,
    'columns_are(table, columns) + extra',
    'Table fou should have the correct columns',
    '    Extra columns:
        "myInt"'
);

SELECT * FROM check_test(
    columns_are( 'fou', ARRAY['id', 'name', 'numb', 'myInt', 'howdy'] ),
    false,
    'columns_are(table, columns) + missing',
    'Table fou should have the correct columns',
    '    Missing columns:
        howdy'
);

SELECT * FROM check_test(
    columns_are( 'fou', ARRAY['id', 'name', 'numb', 'howdy'] ),
    false,
    'columns_are(table, columns) + extra & missing',
    'Table fou should have the correct columns',
    '    Extra columns:
        "myInt"
    Missing columns:
        howdy'
);

/****************************************************************************/
-- Test materialized_views_are().
CREATE FUNCTION test_materialized_views_are() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
BEGIN
    IF pg_version_num() >= 90300 THEN
        EXECUTE $E$
            CREATE MATERIALIZED VIEW public.moo AS SELECT * FROM foo;
            CREATE MATERIALIZED VIEW public.mou AS SELECT * FROM fou;
        $E$;
        
        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( 'public', ARRAY['mou', 'moo'], 'whatever' ),
            true,
            'materialized_views_are(schema, materialized_views, desc)',
            'whatever',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( 'public', ARRAY['mou', 'moo'] ),
            true,
            'materialized_views_are(schema, materialized_views)',
            'Schema public should have the correct materialized views',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( ARRAY['mou', 'moo'] ),
            true,
            'materialized_views_are(views)',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct materialized views',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( ARRAY['mou', 'moo'], 'whatever' ),
            true,
            'materialized_views_are(views, desc)',
            'whatever',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( 'public', ARRAY['mou', 'moo', 'bar'] ),
            false,
            'materialized_views_are(schema, materialized_views) missing',
            'Schema public should have the correct materialized views',
            '    Missing Materialized views:
        bar'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( ARRAY['mou', 'moo', 'bar'] ),
            false,
            'materialized_views_are(materialized_views) missing',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct materialized views',
            '    Missing Materialized views:
        bar'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( 'public', ARRAY['mou'] ),
            false,
            'materialized_views_are(schema, materialized_views) extra',
            'Schema public should have the correct materialized views',
            '    Extra Materialized views:
        moo'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( ARRAY['mou'] ),
            false,
            'materialized_views_are(materialized_views) extra',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct materialized views',
            '    Extra Materialized views:
        moo'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( 'public', ARRAY['bar', 'baz'] ),
            false,
            'materialized_views_are(schema, materialized_views) extra and missing',
            'Schema public should have the correct materialized views',
            '    Extra Materialized views:
        mo[ou]
        mo[ou]
    Missing Materialized views:
        ba[rz]
        ba[rz]',
            true
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            materialized_views_are( ARRAY['bar', 'baz'] ),
            false,
            'materialized_views_are(materialized_views) extra and missing',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct materialized views',
            '    Extra Materialized views:' || '
        mo[ou]
        mo[ou]
    Missing Materialized views:' || '
        ba[rz]
        ba[rz]',
            true
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    ELSE
        -- Fake it with views_are
        FOR tap IN SELECT * FROM check_test(
            views_are( 'public', ARRAY['vou', 'voo'], 'whatever' ),
            true,
            'materialized_views_are(schema, materialized_views, desc)',
            'whatever',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( 'public', ARRAY['vou', 'voo'] ),
            true,
            'materialized_views_are(schema, materialized_views)',
            'Schema public should have the correct views',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( ARRAY['vou', 'voo'] ),
            true,
            'materialized_views_are(views)',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( ARRAY['vou', 'voo'], 'whatever' ),
            true,
            'materialized_views_are(views, desc)',
            'whatever',
            ''
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( 'public', ARRAY['vou', 'voo', 'bar'] ),
            false,
            'materialized_views_are(schema, materialized_views) missing',
            'Schema public should have the correct views',
            '    Missing views:
        bar'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( ARRAY['vou', 'voo', 'bar'] ),
            false,
            'materialized_views_are(materialized_views) missing',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
            '    Missing views:
        bar'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( 'public', ARRAY['vou'] ),
            false,
            'materialized_views_are(schema, materialized_views) extra',
            'Schema public should have the correct views',
            '    Extra views:
        voo'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( ARRAY['vou'] ),
            false,
            'materialized_views_are(materialized_views) extra',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
            '    Extra views:
        voo'
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( 'public', ARRAY['bar', 'baz'] ),
            false,
            'materialized_views_are(schema, materialized_views) extra and missing',
            'Schema public should have the correct views',
            '    Extra views:
        vo[ou]
        vo[ou]
    Missing views:
        ba[rz]
        ba[rz]',
            true
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;

        FOR tap IN SELECT * FROM check_test(
            views_are( ARRAY['bar', 'baz'] ),
            false,
            'materialized_views_are(materialized_views) extra and missing',
            'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct views',
            '    Extra views:' || '
        vo[ou]
        vo[ou]
    Missing views:' || '
        ba[rz]
        ba[rz]',
            true
        ) AS b LOOP
            RETURN NEXT tap.b;
        END LOOP;
    end IF;
    
    RETURN;
END; 
$$LANGUAGE PLPGSQL;

SELECT * FROM test_materialized_views_are();

/****************************************************************************/
-- tests for foreign_tables_are functions

CREATE FUNCTION test_foreign_tables_are() RETURNS SETOF TEXT AS $$
DECLARE
    tap record;
    i int;
BEGIN
    IF pg_version_num() >= 90100 THEN
        -- setup tables for tests
        EXECUTE $setup$
        CREATE FOREIGN DATA WRAPPER null_fdw;
        CREATE SERVER server_null_fdw FOREIGN DATA WRAPPER null_fdw;
        CREATE FOREIGN TABLE public.ft_foo(
            id      INT ,
            "name"  TEXT ,
            num     NUMERIC(10, 2),
          "myInt" int
        )
        SERVER server_null_fdw ;
        CREATE FOREIGN TABLE public.ft_bar(
            id    INT 
        )
        SERVER server_null_fdw ;
        $setup$;

-- foreign_tables_are( schema, tables, description )
-- CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME, NAME[], TEXT )
   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( 'public', ARRAY['ft_foo', 'ft_bar'], 'Correct foreign tables in named schema with custom message' ),
    true,
    'foreign_tables_are(schema, tables, desc)',
    'Correct foreign tables in named schema with custom message',
    ''
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;
-- foreign_tables_are( tables, description )
--CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME[], TEXT )
   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( ARRAY['ft_foo', 'ft_bar'], 'Correct foreign tables in search_path with custom message' ),
    true,
    'foreign_tables_are(tables, desc)',
    'Correct foreign tables in search_path with custom message',
    ''
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;


-- foreign_tables_are( schema, tables )
-- CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME, NAME[] )
   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( 'public', ARRAY['ft_foo', 'ft_bar'] ),
    true,
    'foreign_tables_are(schema, tables)',
    'Schema public should have the correct foreign tables',
    ''
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

-- foreign_tables_are( tables )
-- +CREATE OR REPLACE FUNCTION foreign_tables_are ( NAME[] )
   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( ARRAY['ft_foo', 'ft_bar'] ),
    true,
    'foreign_tables_are(tables)',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct foreign tables',
    ''
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;


   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( 'public', ARRAY['ft_foo', 'ft_bar', 'bar'] ),
    false,
    'foreign_tables_are(schema, tables) missing',
    'Schema public should have the correct foreign tables',
    '    Missing foreign tables:
        bar'
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( ARRAY['ft_foo', 'ft_bar', 'bar'] ),
    false,
    'foreign_tables_are(tables) missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct foreign tables',
    '    Missing foreign tables:
        bar'
      ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( 'public', ARRAY['ft_foo'] ),
    false,
    'foreign_tables_are(schema, tables) extra',
    'Schema public should have the correct foreign tables',
    '    Extra foreign tables:
        ft_bar'
      ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( ARRAY['ft_foo'] ),
    false,
    'foreign_tables_are(tables) extra',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct foreign tables',
    '    Extra foreign tables:
        ft_bar'
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( 'public', ARRAY['bar', 'baz'] ),
    false,
    'foreign_tables_are(schema, tables) extra and missing',
    'Schema public should have the correct foreign tables',
    '    Extra foreign tables:
        ft_(?:bar|foo)
        ft_(?:foo|bar)
    Missing foreign tables:
        ba[rz]
        ba[rz]',
    true
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

   FOR tap IN SELECT * FROM check_test(
    foreign_tables_are( ARRAY['bar', 'baz'] ),
    false,
    'foreign_tables_are(tables) extra and missing',
    'Search path ' || pg_catalog.current_setting('search_path') || ' should have the correct foreign tables',
    '    Extra foreign tables:' || '
        ft_[fobar]+
        ft_[fobar]+
    Missing foreign tables:' || '
        ba[rz]
        ba[rz]',
    true
    ) AS r LOOP
            RETURN NEXT tap.r;
        END LOOP;

    else
       -- 10 fake pass/fail tests to make test work in older postgres. 
       FOR tap IN SELECT * FROM check_test(pass('pass'), true,
                'foreign_tables_are(schema, tables, desc)'
            , 'pass', '') AS r LOOP
           return next tap.r;
       end loop;
       FOR tap IN SELECT * FROM check_test(pass('pass'), true,
                'foreign_tables_are(tables, desc)'
            , 'pass', '') AS r LOOP
           return next tap.r;
       end loop;
       FOR tap IN SELECT * FROM check_test(pass('pass'), true, 
        'foreign_tables_are(schema, tables)'
        ,'pass' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(pass('pass'), true, 
        'foreign_tables_are(tables)'
        ,'pass' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(schema, tables) missing'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(tables) missing'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(schema, tables) extra'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(tables) extra'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(schema, tables) extra and missing'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
       FOR tap IN SELECT * FROM check_test(fail('fail'), false,
        'foreign_tables_are(tables) extra and missing'
        ,'fail' ,'') AS r LOOP
           RETURN NEXT tap.r;
       END LOOP;
    end if;
    RETURN;
END;
$$ LANGUAGE PLPGSQL;

SELECT * FROM test_foreign_tables_are();

/****************************************************************************/

-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
