drop table if exists test;
create table test (r1 text, v1 text, primary key(r1 asc));

begin transaction isolation level xxx;

insert into test values ('1', 'b');

commit;
