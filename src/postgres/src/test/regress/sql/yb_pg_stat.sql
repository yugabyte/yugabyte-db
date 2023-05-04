-- Test for PG_STAT
-- test FOR LSM idx_scan in pg_stat_user_indexes
create table maintable(c1 INT, c2 TEXT, PRIMARY KEY(c1));
insert into maintable (c1, c2) values (4, 'bob');
create index maintable_idx on maintable (c2) include (c1);
/*+IndexOnlyScan(maintable_idx)*/
select * from maintable where c2='bob';
-- need to sleep for over half a second here since updates to pgstat is hardcoded to 500 milliseconds
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

/*+IndexScan(maintable)*/
select * from maintable where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- negative case where we don't use index scan
/*+SeqScan(maintable)*/
select * from maintable;
select * from maintable where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- test case for primary key scan
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_pkey';
/*+IndexScan(maintable)*/
select * from maintable where c1=4;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_pkey';

-- test case for transaction abort
begin;
/*+IndexScan(maintable)*/
select * from maintable where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';
abort;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- test for table view
create view maintable_view as select * from maintable;
select * from maintable_view where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- test for materialized table view
create materialized view materialized_maintable_view as select * from maintable where c2='bob';
select * from materialized_maintable_view where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- test for refreshing materialized table view
insert into maintable (c1, c2) values (6, 'sol'); 
/*+IndexScan(maintable) IndexScan(materialized_maintable_view)*/
refresh materialized view materialized_maintable_view;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='maintable_idx';

-- test for refreshing materialized table view with index
create index materialized_view_idx on materialized_maintable_view (c2) include (c1);
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='materialized_view_idx';
select * from materialized_maintable_view where c2='bob';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='materialized_view_idx';
refresh materialized view materialized_maintable_view;
select pg_sleep(1);
-- currently, after a refresh materialized view is called, idx_scan is reset 
-- this is not consistent with upstream PG and needs to be fixed
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='materialized_view_idx';

-- test for joined table
create table table2 (c1 INT PRIMARY KEY, c2 TEXT);
insert into table2 (c1, c2) values (8, 'bob');
create index table2_index on table2 (c2) include (c1);
/*+IndexScan(table2) */
explain (costs off) select maintable.c1, table2.c1, table2.c2 from maintable, table2 where table2.c2=maintable.c2;
/*+IndexScan(table2) */
select maintable.c1, table2.c1, table2.c2 from maintable, table2 where table2.c2=maintable.c2;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname in ('maintable_idx', 'table2_index') order by (indexrelname);

/*+IndexScan(maintable)*/
explain (costs off) select table2.c1, table2.c2, maintable.c1 from table2, maintable where table2.c2=maintable.c2;
/*+IndexScan(maintable)*/
select table2.c1, table2.c2, maintable.c1 from table2, maintable where table2.c2=maintable.c2;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname in ('maintable_idx', 'table2_index') order by (indexrelname);

-- test for multitablet table
create table multitablet_table (c1 INT PRIMARY KEY, c2 TEXT) split into 3 tablets;
insert into multitablet_table (c1, c2) values (9, 'caledonia');
create index multitablet_table_index on multitablet_table (c2) include (c1);
select * from multitablet_table where c2='caledonia';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='multitablet_table_index';

-- test for alter table primary key
create table basic_table (c1 INT, c2 TEXT);
insert into basic_table (c1, c2) values (6, '9');
create index basic_table_idx1 on basic_table (c1) include (c2);
create index basic_table_idx2 on basic_table (c2) include (c1);
select * from basic_table where c1=6;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'basic_table_idx.' order by (indexrelname);
select * from basic_table where c2='9';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'basic_table_idx.' order by (indexrelname);

alter table basic_table add primary key (c1);

select pg_sleep(1);
-- currently, alter table primary key resets the idx_scan count to zero
-- this does not happen in upstream Postgres, will need to fix
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'basic_table_idx.' or indexrelname='basic_table_pkey' order by (indexrelname);
select * from basic_table where c1=6;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'basic_table_idx.' or indexrelname='basic_table_pkey' order by (indexrelname);
select * from basic_table where c2='9';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'basic_table_idx.' or indexrelname='basic_table_pkey' order by (indexrelname);

-- test for temporary table index
create temporary table temp_table (c1 INT PRIMARY KEY, c2 TEXT);
insert into temp_table (c1, c2) values (9, 'penguin');
create index temp_index on temp_table (c2) include (c1);
/*+IndexScan(temp_table)*/
explain (costs off) select * from temp_table where c2='penguin';
/*+IndexScan(temp_table)*/
select * from temp_table where c2='penguin';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='temp_index';

-- test for partitioned tables
create table partitioned_table (k INT, v TEXT) partition by range (k);
create table p1 partition of partitioned_table
  for values from (1) to (3);
create table p2 partition of partitioned_table
  for values from (3) to (5);
create table p3 partition of partitioned_table
  for values from (5) to (7);
insert into partitioned_table (k, v) values (2, '2');
insert into partitioned_table (k, v) values (4, '2');
insert into partitioned_table (k, v) values (6, '2');
create index partitioned_idx on partitioned_table (v) include (k);
select * from partitioned_table where v='2' order by (k);

select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'p._v_k_idx' order by (indexrelname);

drop index if exists partitioned_idx;
create index p1idx on p1 (v) include (k);
create index p2idx on p2 (v) include (k);
create index p3idx on p3 (v) include (k);
select * from partitioned_table where v='2' order by (k);

select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'p.idx' order by (indexrelname);
  
-- test for GIN idx_scan increment
create table pendtest (ts tsvector);
create index pendtest_idx on pendtest using gin(ts);
insert into pendtest values (to_tsvector('Lore ipsum'));
/*+IndexScan(pendtest)*/
select * from pendtest where 'ipsu:*'::tsquery @@ ts;

select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='pendtest_idx';

-- negative case where we don't use index scan
/*+SeqScan(pendtest)*/
select * from pendtest;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='pendtest_idx';

-- temp index for GIN table
drop table if exists pendtest cascade;
create temporary table pendtest (ts tsvector);
create index pendtest_idx on pendtest using gin(ts);
insert into pendtest values (to_tsvector('Lore ipsum'));
-- this should be an index scan but is for some reason still a sequential scan
-- we will need to fix this during planning phase, ignores pg_hint_plan
/*+IndexScan(pendtest)*/
explain (costs off) select * from pendtest where ts @@ to_tsquery('ipsu:*');
/*+IndexScan(pendtest)*/
select * from pendtest where ts @@ to_tsquery('ipsu:*');

select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='pendtest_idx';

-- negative case where we don't use index scan
/*+SeqScan(pendtest)*/
explain (costs off) select * from pendtest;
/*+SeqScan(pendtest)*/
select * from pendtest;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname='pendtest_idx';

-- test for colocated table
create database colocated_db with colocation = true;
-- TODO: pg_sleep is a workaround, remove it after fixing of #14519
select pg_sleep(3);
\c colocated_db;
-- TODO: pg_sleep is a workaround, remove it after fixing of #14519
select pg_sleep(3);
create table mycolocatedtable (c1 INT PRIMARY KEY, c2 TEXT, c3 INT);
insert into mycolocatedtable (c1, c2, c3) values (6, '9', 8);
create index mycolocatedtable_index1 on mycolocatedtable (c2);
create index mycolocatedtable_index2 on mycolocatedtable (c3);
select * from mycolocatedtable where c2='9';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'mycolocatedtable_index.' order by (indexrelname);
select * from mycolocatedtable where c3=8;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'mycolocatedtable_index.' order by (indexrelname);

-- test for tablegroup 
create database test_db;
\c test_db;
create tablegroup test_tg;
create table test_t (c1 INT PRIMARY KEY, c2 TEXT, c3 INT) tablegroup test_tg;
insert into test_t (c1, c2, c3) values (6, '9', 8);
create index test_index1 on test_t (c2);
create index test_index2 on test_t (c3);
select * from test_t where c2='9';
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'test_index.' order by (indexrelname);
select * from test_t where c3=8;
select pg_sleep(1);
select indexrelname,idx_scan from pg_stat_user_indexes where indexrelname ~ 'test_index.' order by (indexrelname);