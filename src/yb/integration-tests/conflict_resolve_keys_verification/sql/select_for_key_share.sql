begin transaction isolation level xxx;

select * from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for key share;

select * from test where h1 = '1' and h2 = '2' and r1 = '3' for key share;

select * from test where h1 = '1' and h2 = '2' for key share;

select * from test where h1 = '1' for key share;

select * from test for key share;

select h1, h2, r1, r2 from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for key share;

commit;
