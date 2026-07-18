
begin transaction isolation level xxx;

delete from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

delete from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

commit;
