--drop table if exists test;
--create table test (a int primary key, b int);

insert into test values (1, 2);
insert into test values (2, 3), (4, 5);

begin;
update test set b = b + 1 where a = 4; -- the row becomes (4, 6)
delete from test where a = 2;
commit;

begin;
alter table test drop column b;
commit;
