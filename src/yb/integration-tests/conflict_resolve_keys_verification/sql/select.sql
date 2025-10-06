begin transaction isolation level xxx;

select * from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

select * from test where h1 = '1' and h2 = '2' and r1 = '3';

select * from test where h1 = '1' and h2 = '2';

select * from test where h1 = '1';

select * from test;

commit;
