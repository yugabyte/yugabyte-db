
drop table if exists test;
create table test (h1 text, h2 text, r1 text, r2 text, v1 text, v2 text, primary key((h1, h2), r1, r2));

begin transaction isolation level xxx;

insert into test values ('1', '2', '3', '4', 'a', 'b');

commit;
