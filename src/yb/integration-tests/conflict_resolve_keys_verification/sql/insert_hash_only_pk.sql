drop table if exists test;
create table test (h1 text, v1 text, primary key(h1 hash));

begin transaction isolation level xxx;

insert into test values ('1', 'b');

commit;
