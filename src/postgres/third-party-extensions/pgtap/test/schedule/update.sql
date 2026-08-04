\unset ECHO
\i test/psql.sql

CREATE EXTENSION pgtap VERSION :'old_ver';
SELECT plan(2);
SELECT is(
    (SELECT extversion FROM pg_extension WHERE extname = 'pgtap')
    , :'old_ver'
    , 'Old version of pgtap correctly installed'
);

ALTER EXTENSION pgtap UPDATE TO :'new_ver';

SELECT is(
    (SELECT extversion FROM pg_extension WHERE extname = 'pgtap')
    , :'new_ver'
    , 'pgtap correctly updated'
);

SELECT finish();
