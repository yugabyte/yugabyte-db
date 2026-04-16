create database test_colo_db with colocation = true;
\c test_colo_db
create table test (r1 text, v1 text, primary key(r1 asc)) with (COLOCATION_ID = 20000);

truncate table test;

\c yugabyte
drop database test_colo_db;
