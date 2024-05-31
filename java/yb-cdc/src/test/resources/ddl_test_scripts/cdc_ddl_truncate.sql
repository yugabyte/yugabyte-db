--drop table if exists test;
--create table test (a int primary key, b int);

insert into test values (1, 2);

begin;
insert into test values (5, 6);
commit;

insert into test values (7, 8);

begin;
insert into test values (0, 1);
end;

truncate table test;
