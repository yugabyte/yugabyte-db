BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.age(timestamp '2001-04-10', timestamp '1957-06-13') = '43 years 9 mons 27 days';

SELECT pg_typeof(anon.age(timestamp '2001-04-10')) = 'interval'::REGTYPE;

SELECT anon.concat('foo', 'bar') = 'foobar';

SELECT anon.concat('foo', 'bar', 'baz') = 'foobarbaz';

SELECT anon.date_add('2020-03-19 12:00:00-00:00'::timestamp, '1 week') = '2020-03-26 12:00:00-00:00'::timestamp;

SELECT anon.date_part('day', '2020-03-19 12:00:00-00:00'::timestamp) = 19;

SELECT anon.date_subtract('2020-03-19 12:00:00-00:00'::timestamp, '1 week') = '2020-03-12 12:00:00-00:00'::timestamp;

SELECT anon.date_trunc('month', '2020-03-19 12:00:00-00:00'::timestamp) = '2020-03-01 00:00:00-00:00'::timestamp;

SELECT anon.left('foo', 2) = 'fo';

SELECT anon.length('foo') = 3;

SELECT anon.lower('fOO bAr BAz 123') = 'foo bar baz 123';

SELECT anon.make_date(2020, 3, 19) = '2020-03-19'::date;

SELECT anon.make_time(12, 31, 35.08) = '12:31:35.08'::time;

SELECT anon.md5('foo') = 'acbd18db4cc2f85cedef654fccc4a4d8';

SELECT pg_typeof(anon.now()) = 'timestamp with time zone'::REGTYPE;

SELECT anon.right('foo', 2) = 'oo';

SELECT anon.substr('foo', 2) = 'oo';

SELECT anon.substr('bazel', 3, 2) = 'ze';

SELECT anon.to_char(timestamp '2002-04-20 17:31:12.66', 'HH12:MI:SS') = '05:31:12';

SELECT anon.to_char(interval '15h 2m 12s', 'HH24:MI:SS') = '15:02:12';

SELECT anon.to_char(125, '999') = ' 125';

SELECT anon.to_char(125.8, '999D9') = ' 125.8';

SELECT anon.to_char(-125.8, '999D99S') = '125.80-';

SELECT anon.to_date('05 Dec 2000', 'DD Mon YYYY') = '2000-12-05'::DATE;

SELECT anon.to_number('12,454.8-', '99G999D9S') = -12454.8;

SELECT pg_typeof(anon.to_timestamp('05 Dec 2000', 'DD Mon YYYY')) = 'TIMESTAMP WITH TIME ZONE'::REGTYPE;

SELECT anon.upper('fOO bAr BAz 123') = 'FOO BAR BAZ 123';

ROLLBACK;
