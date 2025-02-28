--
-- Test user-defined multirange of floats
--

select '{[123.001, 5.e9)}'::float8multirange @> 888.882::float8;
create table float8multirange_test(f8mr float8multirange, i int);
