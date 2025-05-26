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

--
-- Test ybctid in RETURNING clause of DML queries
--
\c yugabyte
CREATE TABLE ybctid_test_hash_range (k1 INT, k2 TEXT, v INT, PRIMARY KEY (k1 HASH, k2 ASC));
-- Basic DML queries
-- Queries that return > 1 row are wrapped in a CTE to impose an ordering on the returned rows.
INSERT INTO ybctid_test_hash_range VALUES (1, 'one', 1), (2, 'two', 2), (3, 'three', 3), (4, 'four', 4) RETURNING *, ybctid;
WITH cte AS (UPDATE ybctid_test_hash_range SET v = v + 1 WHERE k1 < 10 RETURNING *, ybctid)
SELECT * FROM cte ORDER BY k1, k2;
UPDATE ybctid_test_hash_range SET k2 = k2 || '_updated', v = v + 1 WHERE k1 = 2 RETURNING *, ybctid;
UPDATE ybctid_test_hash_range SET k1 = k1 + 5, k2 = k2 || '_updated' WHERE k2 = 'three' RETURNING *, ybctid;
UPDATE ybctid_test_hash_range SET k1 = k1 + 5 WHERE ybctid = '\x479eaf48800000042153666f7572000021' RETURNING *, ybctid;
WITH cte AS (DELETE FROM ybctid_test_hash_range WHERE k1 < 10 RETURNING *, ybctid)
SELECT * FROM cte ORDER BY k1, k2;
-- Single shard queries
INSERT INTO ybctid_test_hash_range VALUES (5, 'five', 5) RETURNING *, ybctid;
UPDATE ybctid_test_hash_range SET v = v + 1 WHERE k1 = 5 AND k2 = 'five' RETURNING k1, k2, ybctid;
DELETE FROM ybctid_test_hash_range WHERE k1 = 5 AND k2 = 'five' RETURNING k2, ybctid;
-- INSERT ... ON CONFLICT queries
INSERT INTO ybctid_test_hash_range VALUES (6, 'six', 6), (7, 'seven', 7) ON CONFLICT (k1, k2) DO NOTHING RETURNING *, ybctid;
INSERT INTO ybctid_test_hash_range VALUES (7, 'seven', 7), (8, 'eight', 8) ON CONFLICT (k1, k2) DO NOTHING RETURNING *, ybctid;
INSERT INTO ybctid_test_hash_range VALUES (9, 'nine', 9), (8, 'eight', 8) ON CONFLICT (k1, k2) DO UPDATE SET v = EXCLUDED.v + 1 RETURNING *, ybctid;
-- Expressions in the RETURNING clause
INSERT INTO ybctid_test_hash_range VALUES (10, 'ten', 10), (11, 'eleven', 11)
  RETURNING (k2 || '-' || k1::TEXT) AS keys, (k1 + v) AS nums, (ybctid || '-' || ybctid) AS dblybctid, ybctid;
UPDATE ybctid_test_hash_range SET v = v + 1 WHERE k1 = 10
  RETURNING (k2 || '-' || k1::TEXT) AS keys, (k1 + v) AS nums, (ybctid || '-' || ybctid) AS dblybctid, ybctid;
UPDATE ybctid_test_hash_range SET k2 = k2 || '_updated' WHERE k1 = 11
  RETURNING (k2 || '-' || k1::TEXT) AS keys, (k1 + v) AS nums, (ybctid || '-' || ybctid) AS dblybctid, ybctid;
DELETE FROM ybctid_test_hash_range WHERE k1 = 10
  RETURNING (k2 || '-' || k1::TEXT) AS keys, (k1 + v) AS nums, (ybctid || '-' || ybctid) AS dblybctid, ybctid;
DELETE FROM ybctid_test_hash_range WHERE ybctid = '\x471c99488000000b2153656c6576656e5f75706461746564000021'
  RETURNING (k2 || '-' || k1::TEXT) AS keys, (k1 + v) AS nums, (ybctid || '-' || ybctid) AS dblybctid, ybctid;
-- Triggers modifying key columns
CREATE OR REPLACE FUNCTION increment_k1() RETURNS TRIGGER AS $$
BEGIN
	IF TG_OP = 'DELETE' THEN
		OLD.k1 = OLD.k1 + 2;
		RETURN OLD;
	ELSE
		NEW.k1 = NEW.k1 + 2;
		RETURN NEW;
	END IF;
END;
$$ LANGUAGE plpgsql;
CREATE TRIGGER increment_k1_trigger BEFORE INSERT OR UPDATE OR DELETE ON ybctid_test_hash_range FOR EACH ROW EXECUTE PROCEDURE increment_k1();
-- Returned ybctid should have 0x20
INSERT INTO ybctid_test_hash_range VALUES (30, 'thirty', 30) RETURNING *, ybctid;
-- Returned ybctid should have 0x22
UPDATE ybctid_test_hash_range SET k2 = k2 || '_updated', v = v + 1 WHERE k1 = 32 RETURNING *, ybctid;
-- Returned ybctid should have 0x22
DELETE FROM ybctid_test_hash_range WHERE k1 > 30 AND k1 < 40 RETURNING *, ybctid;

SELECT * FROM ybctid_test_hash_range ORDER BY k1;
DROP TABLE ybctid_test_hash_range;
