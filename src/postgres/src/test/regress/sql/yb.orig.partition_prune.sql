set yb_enable_bitmapscan = true;

create table ab (a int not null, b int not null) partition by list (a);
create table ab_a2 partition of ab for values in(2) partition by list (b);
create table ab_a2_b1 partition of ab_a2 for values in (1);
create table ab_a2_b2 partition of ab_a2 for values in (2);
create table ab_a2_b3 partition of ab_a2 for values in (3);
create table ab_a1 partition of ab for values in(1) partition by list (b);
create table ab_a1_b1 partition of ab_a1 for values in (1);
create table ab_a1_b2 partition of ab_a1 for values in (2);
create table ab_a1_b3 partition of ab_a1 for values in (3);
create table ab_a3 partition of ab for values in(3) partition by list (b);
create table ab_a3_b1 partition of ab_a3 for values in (1);
create table ab_a3_b2 partition of ab_a3 for values in (2);
create table ab_a3_b3 partition of ab_a3 for values in (3);

create index ab_a2_b1_a_idx on ab_a2_b1 (a);
create index ab_a2_b2_a_idx on ab_a2_b2 (a);
create index ab_a2_b3_a_idx on ab_a2_b3 (a);
create index ab_a1_b1_a_idx on ab_a1_b1 (a);
create index ab_a1_b2_a_idx on ab_a1_b2 (a);
create index ab_a1_b3_a_idx on ab_a1_b3 (a);
create index ab_a3_b1_a_idx on ab_a3_b1 (a);
create index ab_a3_b2_a_idx on ab_a3_b2 (a);
create index ab_a3_b3_a_idx on ab_a3_b3 (a);

insert into ab values (1, 1), (2, 1), (3, 1), (1, 2), (1, 3), (2, 2), (2, 3), (3, 3);

set enable_bitmapscan to true;

-- Test Bitmap Scans
create index on ab (b ASC);
explain (analyze, costs off, summary off, timing off)
/*+ bitmapscan(ab) */ select * from ab where (ab.a = 1 AND ab.b = 1);

-- all ab_a(1|2)_b(1|2) partitions
explain (analyze, costs off, summary off, timing off)
/*+ bitmapscan(ab) */ select * from ab where (ab.a = 1 AND ab.b = 1) OR (ab.a = 2 AND ab.b = 2);

-- all ab_a1_* and ab_*_b1 tables
explain (analyze, costs off, summary off, timing off)
/*+ bitmapscan(ab) */ select * from ab where (ab.a = 1 OR ab.b = 1);

reset enable_bitmapscan;
reset yb_enable_bitmapscan;

drop table ab;
