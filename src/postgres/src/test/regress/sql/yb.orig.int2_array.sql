create table int2_array_test (
    k int primary key,
    smallint_arr smallint[],
    int2_arr int2[]
    );

insert into int2_array_test (k, smallint_arr, int2_arr) values (
    1, '{32767,0,-32768}','{-32768,0,32767}');

select * from int2_array_test;

-- Invalid values for smallint
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, '{-32769}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, '{32768}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, NULL, '{-32769}');
insert into int2_array_test (k, smallint_arr, int2_arr) values (2, NULL, '{32768}');

insert into int2_array_test (k, smallint_arr, int2_arr) values (
    10, '{-10000,1,2,3,10000}', NULL);
insert into int2_array_test (k, smallint_arr, int2_arr) values (
    11, NULL, '{-10000,1,2,3,10000}');
select * from int2_array_test order by k;

drop table int2_array_test;