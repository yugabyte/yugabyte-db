drop database if exists test_colo_db;
create database test_colo_db with colocation = true;
\c test_colo_db

drop table if exists test;
create table test (r text, v text, primary key(r asc)) with (COLOCATION_ID = 20000);

begin transaction isolation level xxx;

insert into test values ('1', 'a');

update test set v='b' where r = '1';

delete from test where r = '1';

commit;
