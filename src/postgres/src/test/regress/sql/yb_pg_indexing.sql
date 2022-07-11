-- Some unsupported features
create table idxpart (a int, b int, c text) partition by range (a);
create table idxpart1 partition of idxpart for values from (0) to (10);
create index concurrently on idxpart (a);
drop table idxpart;
