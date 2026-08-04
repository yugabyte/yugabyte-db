--drop table if exists test;
--create table test (a int primary key, b int);

insert into test values (4, 5), (6, 7), (8, 9), (1, 2), (10, 11);

begin;
update test set b = 0 where a in (4, 6);
commit;

alter table test rename b to c;
