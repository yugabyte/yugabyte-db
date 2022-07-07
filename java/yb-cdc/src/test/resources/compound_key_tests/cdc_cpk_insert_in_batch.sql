--drop table if exists test;
--create table test (a int, b int, c int, d int, primary key(a, b));

insert into test values (1, 2, 3, 4), (5, 6, 7, 8);

update test set c = c + 1 where a = 1 and b = 2;
update test set a = a + 1 where c = 7;
