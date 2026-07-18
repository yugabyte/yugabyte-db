CREATE SCHEMA s AUTHORIZATION sandeep;
create temp table temp_foo(i int);
CREATE TABLE s.t2(id INT);
INSERT INTO s.t2 VALUES(10);
SET ROLE sandeep;
CREATE TABLE s.t3(id INT);
