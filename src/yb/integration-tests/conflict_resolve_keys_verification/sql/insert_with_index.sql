
drop table if exists test;
create table test (h1 text, h2 text, r1 text, r2 text, v1 text, v2 text, primary key((h1, h2), r1, r2));
create index test_idx1 on test (v1 asc);
create index test_idx2 on test (v2);

begin transaction isolation level xxx;

insert into test values ('1', '2', '3', '4', 'a', 'b');

select * from test where v1 = 'a';

select * from test where v2 = 'b';

commit;
