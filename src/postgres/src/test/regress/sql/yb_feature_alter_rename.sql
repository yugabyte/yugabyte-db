
--
-- ALTER RENAME
-- rename column, table, database
--

create database test_rename;
create database test_rename1;
\c test_rename

create table foo(a int primary key, b int);
insert into foo (a, b) values (1, 2);
\d
\d foo

alter table foo rename column b to c;
select a, b from foo; -- fail
select a, c from foo;
insert into foo (a, b) values (2, 3); -- fail
insert into foo (a, c) values (3, 4);
\d foo

alter table foo rename to bar;
select * from foo; -- fail
select * from bar;
\d

\c test_rename1;
alter database test_rename rename to test_rename2;
alter database test_rename2 rename to postgres; -- fail
\l
