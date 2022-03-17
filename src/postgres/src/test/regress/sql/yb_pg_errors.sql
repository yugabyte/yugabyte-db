--
-- ERRORS
--

-- bad in postquel, but ok in PostgreSQL
select 1;


--
-- UNSUPPORTED STUFF

-- doesn't work
-- notify pg_class
--

--
-- SELECT

-- this used to be a syntax error, but now we allow an empty target list
select;

-- no such relation
select * from nonesuch;

-- bad name in target list
select nonesuch from pg_database;

-- empty distinct list isn't OK
select distinct from pg_database;

-- bad attribute name on lhs of operator
select * from pg_database where nonesuch = pg_database.datname;

-- bad attribute name on rhs of operator
select * from pg_database where pg_database.datname = nonesuch;

-- bad attribute name in select distinct on
select distinct on (foobar) * from pg_database;

-- grouping with FOR UPDATE
select null from pg_database group by datname for update;
select null from pg_database group by grouping sets (()) for update;


--
-- DELETE

-- missing relation name (this had better not wildcard!)
delete from;

-- no such relation
delete from nonesuch;


--
-- DROP

-- missing relation name (this had better not wildcard!)
drop table;

-- no such relation
drop table nonesuch;


--
-- ALTER TABLE

-- relation renaming

-- missing relation name
alter table rename;

-- no such relation
alter table nonesuch rename to newnonesuch;

-- no such relation
alter table nonesuch rename to stud_emp;

-- conflict
-- TODO(jason): change expected output when issue #1129 is closed or closing.
alter table stud_emp rename to aggtest;

-- self-conflict
-- TODO(jason): change expected output when issue #1129 is closed or closing.
alter table stud_emp rename to stud_emp;


-- attribute renaming

-- no such relation
alter table nonesuchrel rename column nonesuchatt to newnonesuchatt;

-- no such attribute
-- TODO(jason): change expected output when issue #1129 is closed or closing.
alter table emp rename column nonesuchatt to newnonesuchatt;

-- conflict
-- TODO(jason): change expected output when issue #1129 is closed or closing.
alter table emp rename column salary to manager;

-- conflict
-- TODO(jason): change expected output when issue #1129 is closed or closing.
alter table emp rename column salary to oid;


--
-- TRANSACTION STUFF

-- not in a xact
abort;

-- not in a xact
end;


--
-- CREATE AGGREGATE

-- sfunc/finalfunc type disagreement
create aggregate newavg2 (sfunc = int4pl,
			  basetype = int4,
			  stype = int4,
			  finalfunc = int2um,
			  initcond = '0');

-- left out basetype
create aggregate newcnt1 (sfunc = int4inc,
			  stype = int4,
			  initcond = '0');


--
-- DROP INDEX

-- missing index name
drop index;

-- bad index name
drop index 314159;

-- no such index
drop index nonesuch;


--
-- DROP AGGREGATE

-- missing aggregate name
drop aggregate;

-- missing aggregate type
drop aggregate newcnt1;

-- bad aggregate name
drop aggregate 314159 (int);

-- bad aggregate type
drop aggregate newcnt (nonesuch);

-- no such aggregate
drop aggregate nonesuch (int4);

-- no such aggregate for type
drop aggregate newcnt (float4);


--
-- DROP FUNCTION

-- missing function name
drop function ();

-- bad function name
drop function 314159();

-- no such function
drop function nonesuch();


--
-- DROP TYPE

-- missing type name
drop type;

-- bad type name
drop type 314159;

-- no such type
drop type nonesuch;


--
-- DROP OPERATOR

-- missing everything
drop operator;

-- bad operator name
drop operator equals;

-- missing type list
drop operator ===;

-- missing parentheses
drop operator int4, int4;

-- missing operator name
drop operator (int4, int4);

-- missing type list contents
drop operator === ();

-- no such operator
drop operator === (int4);

-- no such operator by that name
drop operator === (int4, int4);

-- no such type1
drop operator = (nonesuch);

-- no such type1
drop operator = ( , int4);

-- no such type1
drop operator = (nonesuch, int4);

-- no such type2
drop operator = (int4, nonesuch);

-- no such type2
drop operator = (int4, );


--
-- DROP RULE

-- missing rule name
drop rule;

-- bad rule name
drop rule 314159;

-- no such rule
drop rule nonesuch on noplace;

-- these postquel variants are no longer supported
drop tuple rule nonesuch;
drop instance rule nonesuch on noplace;
drop rewrite rule nonesuch;

--
-- Check that division-by-zero is properly caught.
--

select 1/0;

select 1::int8/0;

select 1/0::int8;

select 1::int2/0;

select 1/0::int2;

select 1::numeric/0;

select 1/0::numeric;

select 1::float8/0;

select 1/0::float8;

select 1::float4/0;

select 1/0::float4;


--
-- Test psql's reporting of syntax error location
--

xxx;

CREATE foo;

CREATE TABLE ;

CREATE TABLE
\g

INSERT INTO foo VALUES(123) foo;

INSERT INTO 123
VALUES(123);

INSERT INTO foo
VALUES(123) 123
;

-- with a tab
CREATE TABLE foo
  (id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY,
	id3 INTEGER NOT NUL,
   id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL);

-- long line to be truncated on the left
CREATE TABLE foo(id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY, id3 INTEGER NOT NUL,
id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL);

-- long line to be truncated on the right
CREATE TABLE foo(
id3 INTEGER NOT NUL, id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL, id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY);

-- long line to be truncated both ways
CREATE TABLE foo(id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY, id3 INTEGER NOT NUL, id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL);

-- long line to be truncated on the left, many lines
CREATE
TEMPORARY
TABLE
foo(id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY, id3 INTEGER NOT NUL,
id4 INT4
UNIQUE
NOT
NULL,
id5 TEXT
UNIQUE
NOT
NULL)
;

-- long line to be truncated on the right, many lines
CREATE
TEMPORARY
TABLE
foo(
id3 INTEGER NOT NUL, id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL, id INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY)
;

-- long line to be truncated both ways, many lines
CREATE
TEMPORARY
TABLE
foo
(id
INT4
UNIQUE NOT NULL, idx INT4 UNIQUE NOT NULL, idy INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY, id3 INTEGER NOT NUL, id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL,
idz INT4 UNIQUE NOT NULL,
idv INT4 UNIQUE NOT NULL);

-- more than 10 lines...
CREATE
TEMPORARY
TABLE
foo
(id
INT4
UNIQUE
NOT
NULL
,
idm
INT4
UNIQUE
NOT
NULL,
idx INT4 UNIQUE NOT NULL, idy INT4 UNIQUE NOT NULL, id2 TEXT NOT NULL PRIMARY KEY, id3 INTEGER NOT NUL, id4 INT4 UNIQUE NOT NULL, id5 TEXT UNIQUE NOT NULL,
idz INT4 UNIQUE NOT NULL,
idv
INT4
UNIQUE
NOT
NULL);

-- Check that stack depth detection mechanism works and
-- max_stack_depth is not set too high
create function infinite_recurse() returns int as
'select infinite_recurse()' language sql;
\set VERBOSITY terse
select infinite_recurse();

-- YB note: check for unsupported system columns.
CREATE TABLE test_tab1(id INT);
INSERT INTO test_tab1 VALUES (1) RETURNING ctid;
INSERT INTO test_tab1 VALUES (2) RETURNING cmin;
INSERT INTO test_tab1 VALUES (3) RETURNING cmax;
INSERT INTO test_tab1 VALUES (4) RETURNING xmin;
INSERT INTO test_tab1 VALUES (5) RETURNING xmax;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab1;
SELECT ctid FROM test_tab1;
SELECT cmin FROM test_tab1;
SELECT cmax FROM test_tab1;
SELECT xmin FROM test_tab1;
SELECT xmax FROM test_tab1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab1 WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab1 WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab1 WHERE id = 3;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab1 WHERE id = 4;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab1 WHERE id = 5;
SELECT ctid FROM test_tab1 WHERE id = 1;
SELECT cmin FROM test_tab1 WHERE id = 2;
SELECT cmax FROM test_tab1 WHERE id = 3;
SELECT xmin FROM test_tab1 WHERE id = 4;
SELECT xmax FROM test_tab1 WHERE id = 5;
-- With primary key.
CREATE TABLE test_tab2(id INT, PRIMARY KEY(id));
INSERT INTO test_tab2 VALUES (1) RETURNING ctid;
INSERT INTO test_tab2 VALUES (2) RETURNING cmin;
INSERT INTO test_tab2 VALUES (3) RETURNING cmax;
INSERT INTO test_tab2 VALUES (4) RETURNING xmin;
INSERT INTO test_tab2 VALUES (5) RETURNING xmax;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_tab2 WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_tab2 WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_tab2 WHERE id = 3;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_tab2 WHERE id = 4;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_tab2 WHERE id = 5;
SELECT ctid FROM test_tab2 WHERE id = 1;
SELECT cmin FROM test_tab2 WHERE id = 2;
SELECT cmax FROM test_tab2 WHERE id = 3;
SELECT xmin FROM test_tab2 WHERE id = 4;
SELECT xmax FROM test_tab2 WHERE id = 5;
-- All system columns should work for temp TABLE.
CREATE temp TABLE test_temp_tab(id INT, PRIMARY KEY(id));
INSERT INTO test_temp_tab VALUES (1) RETURNING ctid;
INSERT INTO test_temp_tab VALUES (2) RETURNING cmin;
INSERT INTO test_temp_tab VALUES (3) RETURNING cmax;
INSERT INTO test_temp_tab VALUES (4) RETURNING xmin;
INSERT INTO test_temp_tab VALUES (5) RETURNING xmax;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_temp_tab;
SELECT ctid FROM test_temp_tab;
SELECT cmin FROM test_temp_tab;
SELECT cmax FROM test_temp_tab;
SELECT xmin FROM test_temp_tab;
SELECT xmax FROM test_temp_tab;
EXPLAIN (VERBOSE, COSTS OFF) SELECT ctid FROM test_temp_tab WHERE id = 1;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmin FROM test_temp_tab WHERE id = 2;
EXPLAIN (VERBOSE, COSTS OFF) SELECT cmax FROM test_temp_tab WHERE id = 3;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmin FROM test_temp_tab WHERE id = 4;
EXPLAIN (VERBOSE, COSTS OFF) SELECT xmax FROM test_temp_tab WHERE id = 5;
SELECT ctid FROM test_temp_tab WHERE id = 1;
SELECT cmin FROM test_temp_tab WHERE id = 2;
SELECT cmax FROM test_temp_tab WHERE id = 3;
SELECT xmin FROM test_temp_tab WHERE id = 4;
SELECT xmax FROM test_temp_tab WHERE id = 5;
