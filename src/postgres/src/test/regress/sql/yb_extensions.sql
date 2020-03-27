-- Testing pgcrypto.
create extension pgcrypto;

select digest('xyz', 'sha1');

-- Using fixed salt to make test repeatable.
select crypt('new password', '$1$7kF93Vc4');

-- Using count to make test repeatable.
select count(gen_random_uuid());

-- Testing fuzzystrmatch.
create extension fuzzystrmatch;

select levenshtein('YugaByte', 'yugabyte');

select metaphone('yugabyte', 8);

-- Clean up.
drop extension pgcrypto;

-- Expect failure since function should be removed.
select digest('xyz', 'sha1');

drop extension fuzzystrmatch;

-- Expect failure since function should be removed.
select levenshtein('YugaByte', 'yugabyte');
