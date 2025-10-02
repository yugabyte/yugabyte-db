
drop table if exists test;
create table test (h1 text, h2 text, r1 text, r2 text, v1 text, v2 text, primary key((h1, h2), r1, r2));
CREATE UNIQUE INDEX idx_name ON test(v1, v2);

begin transaction isolation level xxx;

insert into test values ('1', '2', '3', '4', 'a', 'b');

select * from test where v1 = 'a' and v2 = 'b';

select * from test where v1 = 'a';

commit;
