-- hash pk
create table ybctid_test_hash(k int primary key, v text);
insert into ybctid_test_hash values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
select ybctid, * from ybctid_test_hash;
select k, v from ybctid_test_hash where ybctid = '\x47121048800000012121';
select ybctid, k, v from ybctid_test_hash order by ybctid;
create index on ybctid_test_hash(v);
explain (costs off)
select ybctid, * from ybctid_test_hash where v = 'one';
select ybctid, * from ybctid_test_hash where v = 'one';
drop table ybctid_test_hash;
-- range pk
create table ybctid_test_range(k int, v text, primary key(k asc));
insert into ybctid_test_range values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
select ybctid, * from ybctid_test_range;
select k, v from ybctid_test_range where ybctid = '\x488000000221';
select ybctid, k, v from ybctid_test_range order by ybctid;
create index on ybctid_test_range(v asc);
explain (costs off)
select ybctid, * from ybctid_test_range where v = 'one';
select ybctid, * from ybctid_test_range where v = 'one';
drop table ybctid_test_range;
-- hash + range pk
create table ybctid_test_hash_range(k1 int, k2 int, k3 text, v text,
                                    primary key((k1, k2) hash, k3 asc));
insert into ybctid_test_hash_range values (1, 4, 'one', 'four'), (2, 3, 'two', 'three'),
                                          (3, 2, 'three', 'two'), (4, 1, 'four', 'one');
select ybctid, * from ybctid_test_hash_range;
select k1, k2, k3, v from ybctid_test_hash_range
  where ybctid = '\x4707b64880000003488000000221537468726565000021';
select ybctid, k1, k2, k3, v from ybctid_test_hash_range order by ybctid;
create index on ybctid_test_hash_range(v desc);
explain (costs off)
select ybctid, * from ybctid_test_hash_range where v = 'one';
select ybctid, * from ybctid_test_hash_range where v = 'one';
drop table ybctid_test_hash_range;
-- no pk, ybctid are random, test can't show them or sort by them
create table ybctid_test_nopk(k int, v text);
insert into ybctid_test_nopk values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
CREATE OR REPLACE FUNCTION rows_by_ybctid() RETURNS TABLE (kp int, vp text)
AS $$
DECLARE rowid bytea;
BEGIN
  FOR rowid IN select ybctid from ybctid_test_nopk order by k
  LOOP
    RETURN QUERY select k, v from ybctid_test_nopk where ybctid = rowid;
  END LOOP;
END
$$ LANGUAGE plpgsql;
select * from rows_by_ybctid();
drop function rows_by_ybctid;
drop table ybctid_test_nopk;
-- colocated tables
create database codb colocation = true;
\c codb
-- with pk
create table ybctid_test_with_pk(k int, v text, primary key(k asc)) with (colocation=true);
insert into ybctid_test_with_pk values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
select ybctid, * from ybctid_test_with_pk;
select k, v from ybctid_test_with_pk where ybctid = '\x488000000121';
select ybctid, k, v from ybctid_test_with_pk order by ybctid;
drop table ybctid_test_with_pk;
-- without pk
create table ybctid_test_without_pk(k int, v text) with (colocation=true);
insert into ybctid_test_without_pk values (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four');
CREATE OR REPLACE FUNCTION rows_by_ybctid() RETURNS TABLE (kp int, vp text)
AS $$
DECLARE rowid bytea;
BEGIN
  FOR rowid IN select ybctid from ybctid_test_without_pk order by k
  LOOP
    RETURN QUERY select k, v from ybctid_test_without_pk where ybctid = rowid;
  END LOOP;
END
$$ LANGUAGE plpgsql;
select * from rows_by_ybctid();
drop function rows_by_ybctid;
drop table ybctid_test_without_pk;

