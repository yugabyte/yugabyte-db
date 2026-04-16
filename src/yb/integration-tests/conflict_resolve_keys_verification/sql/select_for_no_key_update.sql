begin transaction isolation level xxx;

select * from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for no key update;

select * from test where h1 = '1' and h2 = '2' and r1 = '3' for no key update;

select * from test where h1 = '1' and h2 = '2' for no key update;

select * from test where h1 = '1' for no key update;

select * from test for no key update;

select h1, h2, r1, r2 from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4' for no key update;

commit;
