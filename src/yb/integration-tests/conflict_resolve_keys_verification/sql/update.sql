begin transaction isolation level xxx;

update test set v1='b', v2='a' where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

update test set v1=v2 where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

commit;
