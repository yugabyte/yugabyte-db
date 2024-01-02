-- Tests for range data types.

--
-- test input parser
-- (type textrange was already made in test_setup.sql)
--

-- negative tests; should fail
select ''::textrange;
select '-[a,z)'::textrange;
select '[a,z) - '::textrange;
select '(",a)'::textrange;
select '(,,a)'::textrange;
select '(),a)'::textrange;
select '(a,))'::textrange;
select '(],a)'::textrange;
select '(a,])'::textrange;
select '[z,a]'::textrange;

-- should succeed
select '  empty  '::textrange;
select ' ( empty, empty )  '::textrange;
select ' ( " a " " a ", " z " " z " )  '::textrange;
select '(,z)'::textrange;
select '(a,)'::textrange;
select '[,z]'::textrange;
select '[a,]'::textrange;
select '(,)'::textrange;
select '[ , ]'::textrange;
select '["",""]'::textrange;
select '[",",","]'::textrange;
select '["\\","\\"]'::textrange;
select '(\\,a)'::textrange;
select '((,z)'::textrange;
select '([,z)'::textrange;
select '(!,()'::textrange;
select '(!,[)'::textrange;
select '[a,a]'::textrange;
-- these are allowed but normalize to empty:
select '[a,a)'::textrange;
select '(a,a]'::textrange;
select '(a,a)'::textrange;

select numrange(2.0, 1.0);

select numrange(2.0, 3.0) -|- numrange(3.0, 4.0);
select range_adjacent(numrange(2.0, 3.0), numrange(3.1, 4.0));
select range_adjacent(numrange(2.0, 3.0), numrange(3.1, null));
select numrange(2.0, 3.0, '[]') -|- numrange(3.0, 4.0, '()');
select numrange(1.0, 2.0) -|- numrange(2.0, 3.0,'[]');
select range_adjacent(numrange(2.0, 3.0, '(]'), numrange(1.0, 2.0, '(]'));

select numrange(1.1, 3.3) <@ numrange(0.1,10.1);
select numrange(0.1, 10.1) <@ numrange(1.1,3.3);

select numrange(1.1, 2.2) - numrange(2.0, 3.0);
select numrange(1.1, 2.2) - numrange(2.2, 3.0);
select numrange(1.1, 2.2,'[]') - numrange(2.0, 3.0);
select range_minus(numrange(10.1,12.2,'[]'), numrange(110.0,120.2,'(]'));
select range_minus(numrange(10.1,12.2,'[]'), numrange(0.0,120.2,'(]'));

select numrange(4.5, 5.5, '[]') && numrange(5.5, 6.5);
select numrange(1.0, 2.0) << numrange(3.0, 4.0);
select numrange(1.0, 3.0,'[]') << numrange(3.0, 4.0,'[]');
select numrange(1.0, 3.0,'()') << numrange(3.0, 4.0,'()');
select numrange(1.0, 2.0) >> numrange(3.0, 4.0);
select numrange(3.0, 70.0) &< numrange(6.6, 100.0);

select numrange(1.1, 2.2) < numrange(1.0, 200.2);
select numrange(1.1, 2.2) < numrange(1.1, 1.2);

select numrange(1.0, 2.0) + numrange(2.0, 3.0);
select numrange(1.0, 2.0) + numrange(1.5, 3.0);
select numrange(1.0, 2.0) + numrange(2.5, 3.0); -- should fail

select range_merge(numrange(1.0, 2.0), numrange(2.0, 3.0));
select range_merge(numrange(1.0, 2.0), numrange(1.5, 3.0));
select range_merge(numrange(1.0, 2.0), numrange(2.5, 3.0)); -- shouldn't fail

select numrange(1.0, 2.0) * numrange(2.0, 3.0);
select numrange(1.0, 2.0) * numrange(1.5, 3.0);
select numrange(1.0, 2.0) * numrange(2.5, 3.0);

-- test canonical form for int4range
select int4range(1, 10, '[]');
select int4range(1, 10, '[)');
select int4range(1, 10, '(]');
select int4range(1, 10, '()');
select int4range(1, 2, '()');

-- test canonical form for daterange
select daterange('2000-01-10'::date, '2000-01-20'::date, '[]');
select daterange('2000-01-10'::date, '2000-01-20'::date, '[)');
select daterange('2000-01-10'::date, '2000-01-20'::date, '(]');
select daterange('2000-01-10'::date, '2000-01-20'::date, '()');
select daterange('2000-01-10'::date, '2000-01-11'::date, '()');
select daterange('2000-01-10'::date, '2000-01-11'::date, '(]');

-- test bigint ranges
select int8range(10000000000::int8, 20000000000::int8,'(]');
-- test tstz ranges
set timezone to '-08';
select '[2010-01-01 01:00:00 -05, 2010-01-01 02:00:00 -08)'::tstzrange;
-- should fail
select '[2010-01-01 01:00:00 -08, 2010-01-01 02:00:00 -05)'::tstzrange;
set timezone to default;

--
-- Test user-defined range of floats
-- (type float8range was already made in test_setup.sql)
--

--should fail
create type bogus_float8range as range (subtype=float8, subtype_diff=float4mi);

select '[123.001, 5.e9)'::float8range @> 888.882::float8;
create table float8range_test(f8r float8range, i int);
insert into float8range_test values(float8range(-100.00007, '1.111113e9'), 42);
select * from float8range_test;
drop table float8range_test;

--
-- Test range types over domains
--

create domain mydomain as int4;
create type mydomainrange as range(subtype=mydomain);
select '[4,50)'::mydomainrange @> 7::mydomain;
drop domain mydomain;  -- fail
drop domain mydomain cascade;

--
-- Test domains over range types
--

create domain restrictedrange as int4range check (upper(value) < 10);
select '[4,5)'::restrictedrange @> 7;
select '[4,50)'::restrictedrange @> 7; -- should fail
drop domain restrictedrange;

--
-- Test multiple range types over the same subtype
--

create type textrange1 as range(subtype=text, collation="C");
create type textrange2 as range(subtype=text, collation="C");

select textrange1('a','Z') @> 'b'::text;
select textrange2('a','z') @> 'b'::text;

drop type textrange1;
drop type textrange2;

--
-- Test polymorphic type system
--

create function anyarray_anyrange_func(a anyarray, r anyrange)
  returns anyelement as 'select $1[1] + lower($2);' language sql;

select anyarray_anyrange_func(ARRAY[1,2], int4range(10,20));

-- should fail
select anyarray_anyrange_func(ARRAY[1,2], numrange(10,20));

drop function anyarray_anyrange_func(anyarray, anyrange);

-- should fail
create function bogus_func(anyelement)
  returns anyrange as 'select int4range(1,10)' language sql;

-- should fail
create function bogus_func(int)
  returns anyrange as 'select int4range(1,10)' language sql;

create function range_add_bounds(anyrange)
  returns anyelement as 'select lower($1) + upper($1)' language sql;

select range_add_bounds(int4range(1, 17));
select range_add_bounds(numrange(1.0001, 123.123));

create function rangetypes_sql(q anyrange, b anyarray, out c anyelement)
  as $$ select upper($1) + $2[1] $$
  language sql;

select rangetypes_sql(int4range(1,10), ARRAY[2,20]);
select rangetypes_sql(numrange(1,10), ARRAY[2,20]);  -- match failure

--
-- Arrays of ranges
--

select ARRAY[numrange(1.1, 1.2), numrange(12.3, 155.5)];

create table i8r_array (f1 int, f2 int8range[]);
insert into i8r_array values (42, array[int8range(1,10), int8range(2,20)]);
select * from i8r_array;
drop table i8r_array;

--
-- Ranges of arrays
--

create type arrayrange as range (subtype=int4[]);

select arrayrange(ARRAY[1,2], ARRAY[2,1]);
select arrayrange(ARRAY[2,1], ARRAY[1,2]);  -- fail

select array[1,1] <@ arrayrange(array[1,2], array[2,1]);
select array[1,3] <@ arrayrange(array[1,2], array[2,1]);

--
-- Ranges of composites
--

create type two_ints as (a int, b int);
create type two_ints_range as range (subtype = two_ints);

-- with force_parallel_mode on, this exercises tqueue.c's range remapping
select *, row_to_json(upper(t)) as u from
  (values (two_ints_range(row(1,2), row(3,4))),
          (two_ints_range(row(5,6), row(7,8)))) v(t);

drop type two_ints cascade;

--
-- Check behavior when subtype lacks a hash function
--

create type cashrange as range (subtype = money);

set enable_sort = off;  -- try to make it pick a hash setop implementation

select '(2,5)'::cashrange except select '(5,6)'::cashrange;

reset enable_sort;

--
-- OUT/INOUT/TABLE functions
--

create function outparam_succeed(i anyrange, out r anyrange, out t text)
  as $$ select $1, 'foo'::text $$ language sql;

select * from outparam_succeed(int4range(1,2));

create function inoutparam_succeed(out i anyelement, inout r anyrange)
  as $$ select upper($1), $1 $$ language sql;

select * from inoutparam_succeed(int4range(1,2));

create function table_succeed(i anyelement, r anyrange) returns table(i anyelement, r anyrange)
  as $$ select $1, $2 $$ language sql;

select * from table_succeed(123, int4range(1,11));

-- should fail
create function outparam_fail(i anyelement, out r anyrange, out t text)
  as $$ select '[1,10]', 'foo' $$ language sql;

--should fail
create function inoutparam_fail(inout i anyelement, out r anyrange)
  as $$ select $1, '[1,10]' $$ language sql;

--should fail
create function table_fail(i anyelement) returns table(i anyelement, r anyrange)
  as $$ select $1, '[1,10]' $$ language sql;
