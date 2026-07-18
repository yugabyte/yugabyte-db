drop table if exists test;
create table test (h1 text, h2 text, r1 text, r2 text, v1 text, v2 text, primary key((h1, h2), r1, r2));

insert into test values ('1', '2', '3', '4', 'a', 'b');

update test set v1='b', v2='a' where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';

delete from test where h1 = '1' and h2 = '2' and r1 = '3' and r2 = '4';
