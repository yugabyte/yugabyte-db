create table int4_array_test (
    k int primary key,
    int_arr int[],
    int4_arr int4[]
    );

insert into int4_array_test (k, int_arr, int4_arr) values (
    1, '{2147483647,0,-2147483648}','{-2147483648,0,2147483647}');

select * from int4_array_test;

 -- Invalid values for int
insert into int4_array_test (k, int_arr, int4_arr) values (2, '{-2147483649}', NULL);
insert into int4_array_test (k, int_arr, int4_arr) values (2, '{2147483648}', NULL);
insert into int4_array_test (k, int_arr, int4_arr) values (2, NULL, '{-2147483649}');
insert into int4_array_test (k, int_arr, int4_arr) values (2, NULL, '{2147483648}');

insert into int4_array_test (k, int_arr, int4_arr) values (
    10, '{-1000000000,1,2,3,1000000000}', NULL);
insert into int4_array_test (k, int_arr, int4_arr) values (
    11, NULL, '{-1000000000,1,2,3,1000000000}');
select * from int4_array_test order by k;

drop table int4_array_test;