create table view_table(a varchar);
insert into view_table values ('test'), ('hello');

create view vw as select * from view_table;

select * from vw;
select * from vw offset 1;
select * from vw limit 1;

drop view vw;

create schema s;
create table s.t as select 21;
create table "s.t" as select 42;

create view vw1 as select * from s.t;
create view vw2 as select * from "s.t";

select * from vw1, vw2;

drop view vw1;
drop view vw2;
drop table "s.t";
drop table s.t;
drop schema s;
drop table view_table;
