\unset ECHO
\i test/setup.sql

select plan(39);

select ok(true);
select ok(true, 'true');
select ok(NOT false);
select ok(NOT false, 'NOT false');
select ok(3 = 3);
select ok(3 = 3, 'three');

select ok(1 != 2);
select ok(1 <> 2);
select isnt(1,2);
select isnt(1,2,'1=2');

select is( now(), now(), 'now()=now()');
select is( '1 hour'::interval, '1 hour'::interval, '''1 hour''::interval, ''1 hour''::interval');
select is( now()::date, now()::date);
select is( now()::date, now()::date, 'now=now date' );
select isnt( now()::date, now()::date + 1, 'now!=now+1' );

select is( now()::timestamp, now()::timestamp, 'now()=now() timestamp');
select is( now()::date, now()::date, 'now()=now() date');
select is( TRUE, TRUE, 'TRUE=TRUE' );
select isnt( TRUE, FALSE, 'TRUE!=FALSE' );
select is('a'::char, 'a'::char, 'a=a char');
select isnt('a'::char, 'b'::char, 'a!=b char');

select is('a'::text, 'a'::text, 'a=a text');
select isnt('a'::text, 'b'::text, 'a!=b text');
select is(3::int, 3::int, '3=3 int');
select isnt(3::int, 4::int, '3!=4 int');
select is(3::integer, 3::integer, '3=3 integer');
select isnt(3::integer, 4::integer, '3!=4 integer');
select is(3::int2, 3::int2, '3=3 int2');
select isnt(3::int2, 4::int2, '3!=4 int2');
select is(3::int4, 3::int4, '3=3 int4');
select isnt(3::int4, 4::int4, '3!=4 int4');
select is(3::int8, 3::int8, '3=3 int8');
select isnt(3::int8, 4::int8, '3!=4 int8');
select is(3.2::float, 3.2::float, '3.2=3.2 float');
select isnt(3.2::float, 4.5::float, '3.2!=4.5 float');
select is(3.2::float4, 3.2::float4, '3.2=3.2 float4');
select isnt(3.2::float4, 4.5::float4, '3.2!=4.5 float4');
select is(3.2::float8, 3.2::float8, '3.2=3.2 float8');
select isnt(3.2::float8, 4.5::float8, '3.2!=4.5 float8');

select * from finish();
ROLLBACK;
