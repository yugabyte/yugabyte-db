--drop table if exists test;
--create table test (a int primary key, b int, c int);

begin;
insert into test values (7, 8, 9);
update test set b = b + 1, c = c + 1 where a = 7;
insert into test values (4, 5, 6), (34, 35, 45), (1000, 1001, 1004);
insert into test values (32, 12, 20);
delete from test where c = 10;
rollback;
