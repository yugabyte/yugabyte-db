--DROP TABLE IF EXISTS test;
--CREATE TABLE test (a int primary key, b int);

begin;
insert into test values (1, 2);
commit;
