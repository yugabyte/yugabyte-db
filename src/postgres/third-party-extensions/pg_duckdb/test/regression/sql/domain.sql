create domain domainvarchar varchar(5) check (value is not null);
create domain domainnumeric numeric(8,2)  check (value is not null);
create domain domainint4 int4 check (value > 0);
create domain domaintext text check (value is not null);

-- Test tables using domains
create table basictest
           ( testint4 domainint4
           , testtext domaintext
           , testvarchar domainvarchar
           , testnumeric domainnumeric
           );

-- In fact, when we carry out an INSERT operation, the check regarding "domain" is conducted by PostgreSQL rather than DuckDB.
-- When we execute a SELECT operation and the field of the query is "domain", we will convert it to the corresponding base type
-- and let DuckDB handle it.
INSERT INTO basictest values ('88', 'haha', 'short', '123.12');      -- Good
INSERT INTO basictest values ('88', 'haha', 'short text', '123.12'); -- Bad varchar
INSERT INTO basictest values ('88', 'haha', 'short', '123.1212');    -- Truncate numeric

-- not support. It will be converted to the following statement
-- SELECT ('-5'::integer)::domainint4 AS domainint4 FROM pgduckdb.xxx.basictest
SELECT (-5)::domainint4 FROM basictest;

select * from basictest;

select testtext || testvarchar as concat, testnumeric + 42 as sum
from basictest;

select * from basictest where testtext = 'haha';
select * from basictest where testvarchar = 'short';

-- array_domain
create domain domain_int_array as INT[];
CREATE TABLE domain_int_array_1d(a domain_int_array);
INSERT INTO domain_int_array_1d SELECT CAST(a as domain_int_array) FROM (VALUES
    ('{1, 2, 3}'),
    (NULL),
    ('{4, 5, NULL, 7}'),
    ('{}')
) t(a);
SELECT * FROM domain_int_array_1d;

CREATE TABLE domain_int_array_2d(a domainint4[]);
INSERT INTO domain_int_array_2d SELECT CAST(a as domain_int_array) FROM (VALUES
    ('{1, 2, 3}'),
    (NULL),
    ('{4, 5, NULL, 7}'),
    ('{}')
) t(a);
SELECT * FROM domain_int_array_2d;

drop table domain_int_array_2d;
drop table domain_int_array_1d;
drop domain domain_int_array;
drop table basictest;
drop domain domainvarchar restrict;
drop domain domainnumeric restrict;
drop domain domainint4 restrict;
drop domain domaintext;
