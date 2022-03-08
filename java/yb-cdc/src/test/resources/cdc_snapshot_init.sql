insert into test values (1, 2, 3);
insert into test values (2, 3, 4);
insert into test values (3, 4, 5);

begin;
insert into test values (4, 5, 6);
commit;

begin;
delete from test where a = 1;
commit;

begin;
update test set c = 404 where a = 4;
commit;
