insert into test values (1, 2, 3, 4);

begin;
insert into test values (5, 6, 7, 8);
commit;

delete from test where a = 1 and b = 2;

update test set c = c + 1 where a = 5;
