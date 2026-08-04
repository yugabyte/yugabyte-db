
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.init();

--
-- Using the automatic salt and default algorithm
--
SELECT anon.hash(NULL) IS NULL;

SELECT anon.hash('x')
     = anon.digest('x',
                    pg_catalog.current_setting('anon.salt'),
                    pg_catalog.current_setting('anon.algorithm')
                 );

--
-- With a random salt
--
SELECT anon.random_hash('abcd') != anon.random_hash('abcd');

-- Restore a predifened salt and change the algo
SET anon.salt TO '4a6821d6z4e33108gg316093e6182b803d0361';
SET anon.algorithm TO 'md5';
SELECT anon.hash('x');
SET anon.algorithm TO 'sha512';
SELECT anon.hash('x');

-- digest
SELECT anon.digest(NULL,'b','sha512') IS NULL;
SELECT anon.digest('a',NULL,'sha512') IS NULL;
SELECT anon.digest('a','b',NULL) IS NULL;

SELECT anon.digest('a','b','md5') = '187ef4436122d1cc2f40dc2b92f0eba0';
SELECT anon.digest('a','b','sha1') IS NULL;
SELECT anon.digest('a','b','sha224') = 'db3cda86d4429a1d39c148989566b38f7bda0156296bd364ba2f878b';
SELECT anon.digest('a','b','sha256') = 'fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603';
SELECT anon.digest('a','b','sha384') = 'c7be03ba5bcaa384727076db0018e99248e1a6e8bd1b9ef58a9ec9dd4eeebb3f48b836201221175befa74ddc3d35afdd';
SELECT anon.digest('a','b','sha512') = '2d408a0717ec188158278a796c689044361dc6fdde28d6f04973b80896e1823975cdbf12eb63f9e0591328ee235d80e9b5bf1aa6a44f4617ff3caf6400eb172d';

ROLLBACK;
