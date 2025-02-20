-- Creating an index on a partitioned table makes the partitions
-- automatically get the index
create table idxpart (a int, b int, c text) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (10);
create table idxpart2 partition of idxpart for values from (10) to (100)
	partition by range (b);
create table idxpart21 partition of idxpart2 for values from (0) to (100);
create index on idxpart (a);
select relname, relkind, inhparent::regclass
    from pg_class left join pg_index ix on (indexrelid = oid)
	left join pg_inherits on (ix.indexrelid = inhrelid)
	where relname like 'idxpart%' order by relname;
drop table idxpart;

-- Some unsupported features
create table idxpart (a int, b int, c text) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (10);
create index concurrently on idxpart (a);
drop table idxpart;

-- Verify bugfix with query on indexed partitioned table with no partitions
-- https://postgr.es/m/20180124162006.pmapfiznhgngwtjf@alvherre.pgsql
CREATE TABLE idxpart (col1 INT) PARTITION BY RANGE (col1);
CREATE INDEX ON idxpart (col1);
CREATE TABLE idxpart_two (col2 INT);
SELECT col2 FROM idxpart_two fk LEFT OUTER JOIN idxpart pk ON (col1 = col2);
DROP table idxpart, idxpart_two;

-- Verify bugfix with index rewrite on ALTER TABLE / SET DATA TYPE
-- https://postgr.es/m/CAKcux6mxNCGsgATwf5CGMF8g4WSupCXicCVMeKUTuWbyxHOMsQ@mail.gmail.com
CREATE TABLE idxpart (a INT, b TEXT, c INT) PARTITION BY RANGE(a);
CREATE TABLE idxpart1 PARTITION OF idxpart FOR VALUES FROM (MINVALUE) TO (MAXVALUE);
CREATE INDEX partidx_abc_idx ON idxpart (a, b, c);
INSERT INTO idxpart (a, b, c) SELECT i, i, i FROM generate_series(1, 50) i;
ALTER TABLE idxpart ALTER COLUMN c TYPE numeric;
DROP TABLE idxpart;

-- If a table without index is attached as partition to a table with
-- an index, the index is automatically created
create table idxpart (a int, b int, c text) partition by range (a);
create index idxparti on idxpart (a);
create index idxparti2 on idxpart (b, c);
create table idxpart1 (like idxpart);
\d idxpart1
alter table idxpart attach partition idxpart1 for values from (0) to (10);
\d idxpart1
\d+ idxpart1_a_idx
\d+ idxpart1_b_c_idx
drop table idxpart;

-- If a partition already has an index, don't create a duplicative one
create table idxpart (a int, b int) partition by range (a, b);
create table idxpart1 partition of idxpart for values from (0, 0) to (10, 10);
create index on idxpart1 (a, b);
create index on idxpart (a, b);
\d idxpart1
select relname, relkind, inhparent::regclass
    from pg_class left join pg_index ix on (indexrelid = oid)
	left join pg_inherits on (ix.indexrelid = inhrelid)
	where relname like 'idxpart%' order by relname;
drop table idxpart;

-- DROP behavior for partitioned indexes
create table idxpart (a int) partition by range (a);
create index on idxpart (a);
create table idxpart1 partition of idxpart for values from (0) to (10);
-- YB Note: Disabling the below test temporarily because each run of the test can
-- throw one of two errors. The error message displayed contains the first dependency
-- resulting from the scan of pg_depend preventing the delete of the index. The index
-- depends on the partition table itself and on the index created on the parent
-- table, and either of the two error messages are possible.
-- drop index idxpart1_a_idx;	-- no way
drop index idxpart_a_idx;	-- both indexes go away
select relname, relkind from pg_class
  where relname like 'idxpart%' order by relname;
create index on idxpart (a);
drop table idxpart1;		-- the index on partition goes away too
select relname, relkind from pg_class
  where relname like 'idxpart%' order by relname;
drop table idxpart;

-- ALTER INDEX .. ATTACH, error cases
create table idxpart (a int, b int) partition by range (a, b);
create table idxpart1 partition of idxpart for values from (0, 0) to (10, 10);
create index idxpart_a_b_idx on only idxpart (a, b);
create index idxpart1_a_b_idx on idxpart1 (a, b);
create index idxpart1_tst1 on idxpart1 (b, a);
create index idxpart1_tst2 on idxpart1 using hash (a);
create index idxpart1_tst3 on idxpart1 (a, b) where a > 10;

alter index idxpart attach partition idxpart1;
alter index idxpart_a_b_idx attach partition idxpart1;
alter index idxpart_a_b_idx attach partition idxpart_a_b_idx;
alter index idxpart_a_b_idx attach partition idxpart1_b_idx;
alter index idxpart_a_b_idx attach partition idxpart1_tst1;
alter index idxpart_a_b_idx attach partition idxpart1_tst2;
alter index idxpart_a_b_idx attach partition idxpart1_tst3;
-- OK
alter index idxpart_a_b_idx attach partition idxpart1_a_b_idx;
alter index idxpart_a_b_idx attach partition idxpart1_a_b_idx; -- quiet

-- reject dupe
create index idxpart1_2_a_b on idxpart1 (a, b);
alter index idxpart_a_b_idx attach partition idxpart1_2_a_b;
drop table idxpart;
-- make sure everything's gone
select indexrelid::regclass, indrelid::regclass
  from pg_index where indexrelid::regclass::text like 'idxpart%';

-- Don't auto-attach incompatible indexes
create table idxpart (a int, b int) partition by range (a);
create table idxpart1 (a int, b int);
-- YB Note: "hash" index is automatically converted to "lsm".
create index on idxpart1 using hash (a);
\d idxpart1_a_idx
create index on idxpart1 (a) where b > 1;
create index on idxpart1 ((a + 0));
create index on idxpart1 (a, a);
create index on idxpart (a);
-- YB Note: Since idxpart1_a_idx is converted to "lsm", it now has the same
-- signature as the index on the parent table. Hence, when idxpart1 is
-- attached as a child partition, a separate index is not created on idxpart1
-- to match the parent. Instead idxpart1_a_idx is attached to the partitioned
-- index on the parent.
alter table idxpart attach partition idxpart1 for values from (0) to (1000);
\d idxpart1
\d idxpart1_a_idx
drop table idxpart;

-- If CREATE INDEX ONLY, don't create indexes on partitions; and existing
-- indexes on partitions don't change parent.  ALTER INDEX ATTACH can change
-- the parent after the fact.
create table idxpart (a int) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (100);
create table idxpart2 partition of idxpart for values from (100) to (1000)
  partition by range (a);
create table idxpart21 partition of idxpart2 for values from (100) to (200);
create table idxpart22 partition of idxpart2 for values from (200) to (300);
create index on idxpart22 (a);
create index on only idxpart2 (a);
create index on idxpart (a);
-- Here we expect that idxpart1 and idxpart2 have a new index, but idxpart21
-- does not; also, idxpart22 is not attached.
\d idxpart1
\d idxpart2
\d idxpart21
select indexrelid::regclass, indrelid::regclass, inhparent::regclass
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
where indexrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
alter index idxpart2_a_idx attach partition idxpart22_a_idx;
select indexrelid::regclass, indrelid::regclass, inhparent::regclass
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
where indexrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
-- attaching idxpart22 is not enough to set idxpart22_a_idx valid ...
alter index idxpart2_a_idx attach partition idxpart22_a_idx;
\d idxpart2
-- ... but this one is.
create index on idxpart21 (a);
alter index idxpart2_a_idx attach partition idxpart21_a_idx;
\d idxpart2
drop table idxpart;

-- When a table is attached a partition and it already has an index, a
-- duplicate index should not get created, but rather the index becomes
-- attached to the parent's index.
create table idxpart (a int, b int, c text) partition by range (a);
create index idxparti on idxpart (a);
create index idxparti2 on idxpart (b, c);
create table idxpart1 (like idxpart including indexes);
\d idxpart1
select relname, relkind, inhparent::regclass
    from pg_class left join pg_index ix on (indexrelid = oid)
	left join pg_inherits on (ix.indexrelid = inhrelid)
	where relname like 'idxpart%' order by relname;
alter table idxpart attach partition idxpart1 for values from (0) to (10);
\d idxpart1
select relname, relkind, inhparent::regclass
    from pg_class left join pg_index ix on (indexrelid = oid)
	left join pg_inherits on (ix.indexrelid = inhrelid)
	where relname like 'idxpart%' order by relname;
drop table idxpart;

-- Verify that attaching an invalid index does not mark the parent index valid.
-- On the other hand, attaching a valid index marks not only its direct
-- ancestor valid, but also any indirect ancestor that was only missing the one
-- that was just made valid
create table idxpart (a int, b int) partition by range (a);
create table idxpart1 partition of idxpart for values from (1) to (1000) partition by range (a);
create table idxpart11 partition of idxpart1 for values from (1) to (100);
create index on only idxpart1 (a);
create index on only idxpart (a);
-- this results in two invalid indexes:
select relname, indisvalid from pg_class join pg_index on indexrelid = oid
   where relname like 'idxpart%' order by relname;
-- idxpart1_a_idx is not valid, so idxpart_a_idx should not become valid:
alter index idxpart_a_idx attach partition idxpart1_a_idx;
select relname, indisvalid from pg_class join pg_index on indexrelid = oid
   where relname like 'idxpart%' order by relname;
-- after creating and attaching this, both idxpart1_a_idx and idxpart_a_idx
-- should become valid
create index on idxpart11 (a);
alter index idxpart1_a_idx attach partition idxpart11_a_idx;
select relname, indisvalid from pg_class join pg_index on indexrelid = oid
   where relname like 'idxpart%' order by relname;
drop table idxpart;

-- verify dependency handling during ALTER TABLE DETACH PARTITION
create table idxpart (a int) partition by range (a);
create table idxpart1 (like idxpart);
create index on idxpart1 (a);
create index on idxpart (a);
create table idxpart2 (like idxpart);
alter table idxpart attach partition idxpart1 for values from (0000) to (1000);
alter table idxpart attach partition idxpart2 for values from (1000) to (2000);
create table idxpart3 partition of idxpart for values from (2000) to (3000);
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;
-- a) after detaching partitions, the indexes can be dropped independently
alter table idxpart detach partition idxpart1;
alter table idxpart detach partition idxpart2;
alter table idxpart detach partition idxpart3;
drop index idxpart1_a_idx;
drop index idxpart2_a_idx;
drop index idxpart3_a_idx;
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;
drop table idxpart, idxpart1, idxpart2, idxpart3;
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;

create table idxpart (a int) partition by range (a);
create table idxpart1 (like idxpart);
create index on idxpart1 (a);
create index on idxpart (a);
create table idxpart2 (like idxpart);
alter table idxpart attach partition idxpart1 for values from (0000) to (1000);
alter table idxpart attach partition idxpart2 for values from (1000) to (2000);
create table idxpart3 partition of idxpart for values from (2000) to (3000);
-- b) after detaching, dropping the index on parent does not remove the others
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;
alter table idxpart detach partition idxpart1;
alter table idxpart detach partition idxpart2;
alter table idxpart detach partition idxpart3;
drop index idxpart_a_idx;
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;
drop table idxpart, idxpart1, idxpart2, idxpart3;
select relname, relkind from pg_class where relname like 'idxpart%' order by relname;

-- Verify that expression indexes inherit correctly
create table idxpart (a int, b int) partition by range (a);
create table idxpart1 (like idxpart);
create index on idxpart1 ((a + b));
create index on idxpart ((a + b));
create table idxpart2 (like idxpart);
alter table idxpart attach partition idxpart1 for values from (0000) to (1000);
alter table idxpart attach partition idxpart2 for values from (1000) to (2000);
create table idxpart3 partition of idxpart for values from (2000) to (3000);
select relname as child, inhparent::regclass as parent, pg_get_indexdef as childdef
  from pg_class join pg_inherits on inhrelid = oid,
  lateral pg_get_indexdef(pg_class.oid)
  where relkind in ('i', 'I') and relname like 'idxpart%' order by relname;
drop table idxpart;

-- Verify behavior for collation (mis)matches
create table idxpart (a text) partition by range (a);
create table idxpart1 (like idxpart);
create table idxpart2 (like idxpart);
create index on idxpart2 (a collate "POSIX");
create index on idxpart2 (a);
create index on idxpart2 (a collate "C");
alter table idxpart attach partition idxpart1 for values from ('aaa') to ('bbb');
alter table idxpart attach partition idxpart2 for values from ('bbb') to ('ccc');
create table idxpart3 partition of idxpart for values from ('ccc') to ('ddd');
create index on idxpart (a collate "C");
create table idxpart4 partition of idxpart for values from ('ddd') to ('eee');
select relname as child, inhparent::regclass as parent, pg_get_indexdef as childdef
  from pg_class left join pg_inherits on inhrelid = oid,
  lateral pg_get_indexdef(pg_class.oid)
  where relkind in ('i', 'I') and relname like 'idxpart%' order by relname;
drop table idxpart;

-- Verify behavior for opclass (mis)matches
create table idxpart (a text) partition by range (a);
create table idxpart1 (like idxpart);
create table idxpart2 (like idxpart);
create index on idxpart2 (a);
alter table idxpart attach partition idxpart1 for values from ('aaa') to ('bbb');
alter table idxpart attach partition idxpart2 for values from ('bbb') to ('ccc');
create table idxpart3 partition of idxpart for values from ('ccc') to ('ddd');
create index on idxpart (a text_pattern_ops);
create table idxpart4 partition of idxpart for values from ('ddd') to ('eee');
-- must *not* have attached the index we created on idxpart2
select relname as child, inhparent::regclass as parent, pg_get_indexdef as childdef
  from pg_class left join pg_inherits on inhrelid = oid,
  lateral pg_get_indexdef(pg_class.oid)
  where relkind in ('i', 'I') and relname like 'idxpart%' order by relname;
drop index idxpart_a_idx;
create index on only idxpart (a text_pattern_ops);
-- must reject
alter index idxpart_a_idx attach partition idxpart2_a_idx;
drop table idxpart;

-- Verify that attaching indexes maps attribute numbers correctly
create table idxpart (col1 int, a int, col2 int, b int) partition by range (a);
create table idxpart1 (b int, col1 int, col2 int, col3 int, a int);
alter table idxpart drop column col1, drop column col2;
alter table idxpart1 drop column col1, drop column col2, drop column col3;
alter table idxpart attach partition idxpart1 for values from (0) to (1000);
create index idxpart_1_idx on only idxpart (b, a);
create index idxpart1_1_idx on idxpart1 (b, a);
create index idxpart1_1b_idx on idxpart1 (b);
-- test expressions and partial-index predicate, too
create index idxpart_2_idx on only idxpart ((b + a)) where a > 1;
create index idxpart1_2_idx on idxpart1 ((b + a)) where a > 1;
create index idxpart1_2b_idx on idxpart1 ((a + b)) where a > 1;
create index idxpart1_2c_idx on idxpart1 ((b + a)) where b > 1;
alter index idxpart_1_idx attach partition idxpart1_1b_idx;	-- fail
alter index idxpart_1_idx attach partition idxpart1_1_idx;
alter index idxpart_2_idx attach partition idxpart1_2b_idx;	-- fail
alter index idxpart_2_idx attach partition idxpart1_2c_idx;	-- fail
alter index idxpart_2_idx attach partition idxpart1_2_idx;	-- ok
select relname as child, inhparent::regclass as parent, pg_get_indexdef as childdef
  from pg_class left join pg_inherits on inhrelid = oid,
  lateral pg_get_indexdef(pg_class.oid)
  where relkind in ('i', 'I') and relname like 'idxpart%' order by relname;
drop table idxpart;

-- Make sure the partition columns are mapped correctly
create table idxpart (a int, b int, c text) partition by range (a);
create index idxparti on idxpart (a);
create index idxparti2 on idxpart (c, b);
create table idxpart1 (c text, a int, b int);
alter table idxpart attach partition idxpart1 for values from (0) to (10);
create table idxpart2 (c text, a int, b int);
create index on idxpart2 (a);
create index on idxpart2 (c, b);
alter table idxpart attach partition idxpart2 for values from (10) to (20);
select c.relname, pg_get_indexdef(indexrelid)
  from pg_class c join pg_index i on c.oid = i.indexrelid
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- Verify that columns are mapped correctly in expression indexes
create table idxpart (col1 int, col2 int, a int, b int) partition by range (a);
create table idxpart1 (col2 int, b int, col1 int, a int);
create table idxpart2 (col1 int, col2 int, b int, a int);
alter table idxpart drop column col1, drop column col2;
alter table idxpart1 drop column col1, drop column col2;
alter table idxpart2 drop column col1, drop column col2;
create index on idxpart2 (abs(b));
alter table idxpart attach partition idxpart2 for values from (0) to (1);
create index on idxpart (abs(b));
create index on idxpart ((b + 1));
alter table idxpart attach partition idxpart1 for values from (1) to (2);
select c.relname, pg_get_indexdef(indexrelid)
  from pg_class c join pg_index i on c.oid = i.indexrelid
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- Verify that columns are mapped correctly for WHERE in a partial index
create table idxpart (col1 int, a int, col3 int, b int) partition by range (a);
alter table idxpart drop column col1, drop column col3;
create table idxpart1 (col1 int, col2 int, col3 int, col4 int, b int, a int);
alter table idxpart1 drop column col1, drop column col2, drop column col3, drop column col4;
alter table idxpart attach partition idxpart1 for values from (0) to (1000);
create table idxpart2 (col1 int, col2 int, b int, a int);
create index on idxpart2 (a) where b > 1000;
alter table idxpart2 drop column col1, drop column col2;
alter table idxpart attach partition idxpart2 for values from (1000) to (2000);
create index on idxpart (a) where b > 1000;
select c.relname, pg_get_indexdef(indexrelid)
  from pg_class c join pg_index i on c.oid = i.indexrelid
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- Column number mapping: dropped columns in the partition
create table idxpart1 (drop_1 int, drop_2 int, col_keep int, drop_3 int);
alter table idxpart1 drop column drop_1;
alter table idxpart1 drop column drop_2;
alter table idxpart1 drop column drop_3;
create index on idxpart1 (col_keep);
create table idxpart (col_keep int) partition by range (col_keep);
create index on idxpart (col_keep);
alter table idxpart attach partition idxpart1 for values from (0) to (1000);
\d idxpart
\d idxpart1
select attrelid::regclass, attname, attnum from pg_attribute
  where attrelid::regclass::text like 'idxpart%' and attnum > 0
  order by attrelid::regclass, attnum;
drop table idxpart;

-- Column number mapping: dropped columns in the parent table
create table idxpart(drop_1 int, drop_2 int, col_keep int, drop_3 int) partition by range (col_keep);
alter table idxpart drop column drop_1;
alter table idxpart drop column drop_2;
alter table idxpart drop column drop_3;
create table idxpart1 (col_keep int);
create index on idxpart1 (col_keep);
create index on idxpart (col_keep);
alter table idxpart attach partition idxpart1 for values from (0) to (1000);
\d idxpart
\d idxpart1
select attrelid::regclass, attname, attnum from pg_attribute
  where attrelid::regclass::text like 'idxpart%' and attnum > 0
  order by attrelid::regclass, attnum;
drop table idxpart;

--
-- Constraint-related indexes
--

-- Verify that it works to add primary key / unique to partitioned tables
create table idxpart (a int primary key, b int) partition by range (a);
\d idxpart
-- multiple primary key on child should fail
create table failpart partition of idxpart (b primary key) for values from (0) to (100);
drop table idxpart;
-- primary key on child is okay if there's no PK in the parent, though
create table idxpart (a int) partition by range (a);
create table idxpart1pk partition of idxpart (a primary key) for values from (0) to (100);
\d idxpart1pk
drop table idxpart;

-- Failing to use the full partition key is not allowed
create table idxpart (a int unique, b int) partition by range (a, b);
create table idxpart (a int, b int unique) partition by range (a, b);
create table idxpart (a int primary key, b int) partition by range (b, a);
create table idxpart (a int, b int primary key) partition by range (b, a);

-- OK if you use them in some other order
create table idxpart (a int, b int, c text, primary key  (a, b, c)) partition by range (b, c, a);
drop table idxpart;

-- not other types of index-based constraints
create table idxpart (a int, exclude (a with = )) partition by range (a);

-- no expressions in partition key for PK/UNIQUE
create table idxpart (a int primary key, b int) partition by range ((b + a));
create table idxpart (a int unique, b int) partition by range ((b + a));

-- use ALTER TABLE to add a primary key
create table idxpart (a int, b int, c text) partition by range (a, b);
alter table idxpart add primary key (a);	-- not an incomplete one though
alter table idxpart add primary key (a, b);	-- this works
\d idxpart
create table idxpart1 partition of idxpart for values from (0, 0) to (1000, 1000);
\d idxpart1
drop table idxpart;

-- use ALTER TABLE to add a unique constraint
create table idxpart (a int, b int) partition by range (a, b);
alter table idxpart add unique (a);			-- not an incomplete one though
alter table idxpart add unique (b, a);		-- this works
\d idxpart
drop table idxpart;

-- Exclusion constraints cannot be added
create table idxpart (a int, b int) partition by range (a);
alter table idxpart add exclude (a with =);
drop table idxpart;

-- When (sub)partitions are created, they also contain the constraint
create table idxpart (a int, b int, primary key (a, b)) partition by range (a, b);
create table idxpart1 partition of idxpart for values from (1, 1) to (10, 10);
create table idxpart2 partition of idxpart for values from (10, 10) to (20, 20)
  partition by range (b);
create table idxpart21 partition of idxpart2 for values from (10) to (15);
create table idxpart22 partition of idxpart2 for values from (15) to (20);
create table idxpart3 (b int not null, a int not null);
alter table idxpart attach partition idxpart3 for values from (20, 20) to (30, 30);
select conname, contype, conrelid::regclass, conindid::regclass, conkey
  from pg_constraint where conrelid::regclass::text like 'idxpart%'
  order by conname;
drop table idxpart;

-- Verify that multi-layer partitioning honors the requirement that all
-- columns in the partition key must appear in primary/unique key
create table idxpart (a int, b int, primary key (a)) partition by range (a);
create table idxpart2 partition of idxpart
for values from (0) to (1000) partition by range (b); -- fail
drop table idxpart;

-- Ditto for the ATTACH PARTITION case
create table idxpart (a int unique, b int) partition by range (a);
create table idxpart1 (a int not null, b int, unique (a, b))
  partition by range (a, b);
alter table idxpart attach partition idxpart1 for values from (1) to (1000);
DROP TABLE idxpart, idxpart1;

-- Multi-layer partitioning works correctly in this case:
create table idxpart (a int, b int, primary key (a, b)) partition by range (a);
create table idxpart2 partition of idxpart for values from (0) to (1000) partition by range (b);
create table idxpart21 partition of idxpart2 for values from (0) to (1000);
select conname, contype, conrelid::regclass, conindid::regclass, conkey
  from pg_constraint where conrelid::regclass::text like 'idxpart%'
  order by conname;
drop table idxpart;

-- If a partitioned table has a unique/PK constraint, then it's not possible
-- to drop the corresponding constraint in the children; nor it's possible
-- to drop the indexes individually.  Dropping the constraint in the parent
-- gets rid of the lot.
create table idxpart (i int) partition by hash (i);
create table idxpart0 partition of idxpart (i) for values with (modulus 2, remainder 0);
create table idxpart1 partition of idxpart (i) for values with (modulus 2, remainder 1);
alter table idxpart0 add primary key(i);
alter table idxpart add primary key(i);
select indrelid::regclass, indexrelid::regclass, inhparent::regclass, indisvalid,
  conname, conislocal, coninhcount, connoinherit, convalidated
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
  left join pg_constraint con on (idx.indexrelid = con.conindid)
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop index idxpart0_pkey;								-- fail
drop index idxpart1_pkey;								-- fail
alter table idxpart0 drop constraint idxpart0_pkey;		-- fail
alter table idxpart1 drop constraint idxpart1_pkey;		-- fail
alter table idxpart drop constraint idxpart_pkey;		-- ok
select indrelid::regclass, indexrelid::regclass, inhparent::regclass, indisvalid,
  conname, conislocal, coninhcount, connoinherit, convalidated
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
  left join pg_constraint con on (idx.indexrelid = con.conindid)
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- If the partition to be attached already has a primary key, fail if
-- it doesn't match the parent's PK.
CREATE TABLE idxpart (c1 INT PRIMARY KEY, c2 INT, c3 VARCHAR(10)) PARTITION BY RANGE(c1);
CREATE TABLE idxpart1 (LIKE idxpart);
ALTER TABLE idxpart1 ADD PRIMARY KEY (c1, c2);
ALTER TABLE idxpart ATTACH PARTITION idxpart1 FOR VALUES FROM (100) TO (200);
DROP TABLE idxpart, idxpart1;

-- Ditto if there is some distance between the PKs (subpartitioning)
create table idxpart (a int, b int, primary key (a)) partition by range (a);
create table idxpart1 (a int not null, b int) partition by range (a);
create table idxpart11 (a int not null, b int primary key);
alter table idxpart1 attach partition idxpart11 for values from (0) to (1000);
alter table idxpart attach partition idxpart1 for values from (0) to (10000);
drop table idxpart, idxpart1, idxpart11;

-- If a partitioned table has a constraint whose index is not valid,
-- attaching a missing partition makes it valid.
create table idxpart (a int) partition by range (a);
create table idxpart0 (like idxpart);
alter table idxpart0 add primary key (a);
alter table idxpart attach partition idxpart0 for values from (0) to (1000);
alter table only idxpart add primary key (a);
select indrelid::regclass, indexrelid::regclass, inhparent::regclass, indisvalid,
  conname, conislocal, coninhcount, connoinherit, convalidated
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
  left join pg_constraint con on (idx.indexrelid = con.conindid)
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
alter index idxpart_pkey attach partition idxpart0_pkey;
select indrelid::regclass, indexrelid::regclass, inhparent::regclass, indisvalid,
  conname, conislocal, coninhcount, connoinherit, convalidated
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
  left join pg_constraint con on (idx.indexrelid = con.conindid)
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- if a partition has a unique index without a constraint, does not attach
-- automatically; creates a new index instead.
create table idxpart (a int, b int) partition by range (a);
create table idxpart1 (a int not null, b int);
create unique index on idxpart1 (a);
alter table idxpart add primary key (a);
alter table idxpart attach partition idxpart1 for values from (1) to (1000);
select indrelid::regclass, indexrelid::regclass, inhparent::regclass, indisvalid,
  conname, conislocal, coninhcount, connoinherit, convalidated
  from pg_index idx left join pg_inherits inh on (idx.indexrelid = inh.inhrelid)
  left join pg_constraint con on (idx.indexrelid = con.conindid)
  where indrelid::regclass::text like 'idxpart%'
  order by indexrelid::regclass::text collate "C";
drop table idxpart;

-- Can't attach an index without a corresponding constraint
create table idxpart (a int, b int) partition by range (a);
create table idxpart1 (a int not null, b int);
create unique index on idxpart1 (a);
alter table idxpart attach partition idxpart1 for values from (1) to (1000);
alter table only idxpart add primary key (a);
alter index idxpart_pkey attach partition idxpart1_a_idx;	-- fail
drop table idxpart;

-- Test that unique constraints are working
create table idxpart (a int, b text, primary key (a, b)) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (100000);
create table idxpart2 (c int, like idxpart);
insert into idxpart2 (c, a, b) values (42, 572814, 'inserted first');
alter table idxpart2 drop column c;
create unique index on idxpart (a);
alter table idxpart attach partition idxpart2 for values from (100000) to (1000000);
insert into idxpart values (0, 'zero'), (42, 'life'), (2^16, 'sixteen');
insert into idxpart select 2^g, format('two to power of %s', g) from generate_series(15, 17) g;
insert into idxpart values (16, 'sixteen');
insert into idxpart (b, a) values ('one', 142857), ('two', 285714);
insert into idxpart select a * 2, b || b from idxpart where a between 2^16 and 2^19;
insert into idxpart values (572814, 'five');
insert into idxpart values (857142, 'six');
select tableoid::regclass, * from idxpart order by a;
drop table idxpart;

-- test fastpath mechanism for index insertion
create table fastpath (a int, b text, c numeric);
create unique index fpindex1 on fastpath(a);

insert into fastpath values (1, 'b1', 100.00);
insert into fastpath values (1, 'b1', 100.00); -- unique key check

truncate fastpath;
insert into fastpath select generate_series(1,10000), 'b', 100;

-- vacuum the table so as to improve chances of index-only scans. we can't
-- guarantee if index-only scans will be picked up in all cases or not, but
-- that fuzziness actually helps the test.
vacuum fastpath;

set enable_seqscan to false;
set enable_bitmapscan to false;

select sum(a) from fastpath where a = 6456;
select sum(a) from fastpath where a >= 5000 and a < 5700;

-- drop the only index on the table and compute hashes for
-- a few queries which orders the results in various different ways.
drop index fpindex1;
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- now create a multi-column index with both column asc
create index fpindex2 on fastpath(a, b);
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
-- again, vacuum here either forces index-only scans or creates fuzziness
vacuum fastpath;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- same queries with a different kind of index now. the final result must not
-- change irrespective of what kind of index we have.
drop index fpindex2;
create index fpindex3 on fastpath(a desc, b asc);
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
vacuum fastpath;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- repeat again
drop index fpindex3;
create index fpindex4 on fastpath(a asc, b desc);
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
vacuum fastpath;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- and again, this time indexing by (b, a). Note that column "b" has non-unique
-- values.
drop index fpindex4;
create index fpindex5 on fastpath(b asc, a desc);
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
vacuum fastpath;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

-- one last time
drop index fpindex5;
create index fpindex6 on fastpath(b desc, a desc);
truncate fastpath;
insert into fastpath select y.x, 'b' || (y.x/10)::text, 100 from (select generate_series(1,10000) as x) y;
vacuum fastpath;
select md5(string_agg(a::text, b order by a, b asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by a desc, b desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a desc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';
select md5(string_agg(a::text, b order by b, a asc)) from fastpath
	where a >= 1000 and a < 2000 and b > 'b1' and b < 'b3';

drop table fastpath;

-- intentionally leave some objects around
create table idxpart (a int) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (100);
create table idxpart2 partition of idxpart for values from (100) to (1000)
  partition by range (a);
create table idxpart21 partition of idxpart2 for values from (100) to (200);
create table idxpart22 partition of idxpart2 for values from (200) to (300);
create index on idxpart22 (a);
create index on only idxpart2 (a);
alter index idxpart2_a_idx attach partition idxpart22_a_idx;
create index on idxpart (a);
create table idxpart_another (a int, b int, primary key (a, b)) partition by range (a);
create table idxpart_another_1 partition of idxpart_another for values from (0) to (100);

-- Test that covering partitioned indexes work in various cases
create table covidxpart (a int, b int) partition by list (a);
create unique index on covidxpart (a) include (b);
create table covidxpart1 partition of covidxpart for values in (1);
create table covidxpart2 partition of covidxpart for values in (2);
insert into covidxpart values (1, 1);
insert into covidxpart values (1, 1);
create table covidxpart3 (b int, c int, a int);
alter table covidxpart3 drop c;
alter table covidxpart attach partition covidxpart3 for values in (3);
insert into covidxpart values (3, 1);
insert into covidxpart values (3, 1);
create table covidxpart4 (b int, a int);
create unique index on covidxpart4 (a) include (b);
create unique index on covidxpart4 (a);
alter table covidxpart attach partition covidxpart4 for values in (4);
insert into covidxpart values (4, 1);
insert into covidxpart values (4, 1);
create unique index on covidxpart (b) include (a); -- should fail

-- check that detaching a partition also detaches the primary key constraint
create table parted_pk_detach_test (a int primary key) partition by list (a);
create table parted_pk_detach_test1 partition of parted_pk_detach_test for values in (1);
alter table parted_pk_detach_test1 drop constraint parted_pk_detach_test1_pkey;	-- should fail
alter table parted_pk_detach_test detach partition parted_pk_detach_test1;
alter table parted_pk_detach_test1 drop constraint parted_pk_detach_test1_pkey;
drop table parted_pk_detach_test, parted_pk_detach_test1;
create table parted_uniq_detach_test (a int unique) partition by list (a);
create table parted_uniq_detach_test1 partition of parted_uniq_detach_test for values in (1);
alter table parted_uniq_detach_test1 drop constraint parted_uniq_detach_test1_a_key;	-- should fail
alter table parted_uniq_detach_test detach partition parted_uniq_detach_test1;
alter table parted_uniq_detach_test1 drop constraint parted_uniq_detach_test1_a_key;
drop table parted_uniq_detach_test, parted_uniq_detach_test1;

-- Test range indexes with both IN queries and range filters
create table range_in_inequality(r1 int, r2 int, primary key(r1 asc, r2 asc));
insert into range_in_inequality select i/5, i%5 from generate_series(1,20) i;
select * from range_in_inequality;
select * from range_in_inequality where r1 in (1, 3) and r2 > 2;
select * from range_in_inequality where r1 in (0, 1, 3) and r2 > 2;

-- YB Note: Adding cleanup, just in case.
drop table range_in_inequality;
drop table idxpart cascade;
drop table idxpart_another cascade;
