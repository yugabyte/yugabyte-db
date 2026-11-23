BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- test data

CREATE TABLE test_shuffle (
  id SERIAL,
  key   TEXT,
  value  INT
);

INSERT INTO test_shuffle
VALUES
  ( 1, 'a', 40 ),
  ( 2, 'b', 70 ),
  ( 3, 'c', 12 ),
  ( 4, 'd', 33 ),
  ( 5, 'e', 71 ),
  ( 6, 'f', 21 ),
  ( 7, 'g', 29 ),
  ( 8, 'h', 22 ),
  ( 9, 'i', 27 ),
  ( 10, 'j', 51 )
;

CREATE TABLE test_shuffle_backup
AS SELECT * FROM test_shuffle;

SELECT anon.shuffle_column('test_shuffle','value','key');


-- TEST 1 : `shuffle()` does not modify the sum
SELECT sum(a.value) = sum(b.value)
FROM
  test_shuffle a,
  test_shuffle_backup b
;


-- TEST 2 : `shuffle()` does not modify the average
SELECT avg(a.value) = avg(b.value)
FROM
  test_shuffle a,
  test_shuffle_backup b
;

--
-- Issue #198
--
CREATE SCHEMA core;
CREATE TABLE core.users_msk(
  id SERIAL,
  email text,
  kanji_last_name text
);

SAVEPOINT does_not_exists_1;
SELECT anon.shuffle_column('core.does_not_exists', 'kanji_last_name', 'id');
ROLLBACK TO does_not_exists_1;

SAVEPOINT does_not_exists_2;
SELECT anon.shuffle_column('core.users_msk', 'does_not_exists', 'id');
ROLLBACK TO does_not_exists_2;

SAVEPOINT does_not_exists_3;
SELECT anon.shuffle_column('core.users_msk', 'kanji_last_name', 'does_not_exists');
ROLLBACK TO does_not_exists_3;

SELECT anon.shuffle_column('core.users_msk', 'kanji_last_name', 'id');

--
-- SQL injection
--

CREATE TABLE a (
    i SERIAL,
    d TIMESTAMP,
    x INTEGER
);

SAVEPOINT test_injection_1;
SELECT anon.shuffle_column('a; SELECT 1','x','i');
ROLLBACK TO test_injection_1;

SAVEPOINT test_injection_2;
SELECT anon.shuffle_column('a','x; SELECT 1','i') IS FALSE;
ROLLBACK TO test_injection_2;

SAVEPOINT test_injection_3;
SELECT anon.shuffle_column('a','x','i; SELECT 1') IS FALSE;
ROLLBACK TO test_injection_3;

ROLLBACK;
