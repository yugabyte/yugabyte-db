--
-- Tests for pg15 branch stability.
--
-- Basics
create table t1 (id int, name text);

create table t2 (id int primary key, name text);

explain (COSTS OFF) insert into t2 values (1);
insert into t2 values (1);

explain (COSTS OFF) insert into t2 values (2), (3);
insert into t2 values (2), (3);

explain (COSTS OFF) select * from t2 where id = 1;
select * from t2 where id = 1;

explain (COSTS OFF) select * from t2 where id > 1;
select * from t2 where id > 1;

explain (COSTS OFF) update t2 set name = 'John' where id = 1;
update t2 set name = 'John' where id = 1;

explain (COSTS OFF) update t2 set name = 'John' where id > 1;
update t2 set name = 'John' where id > 1;

explain (COSTS OFF) update t2 set id = id + 4 where id = 1;
update t2 set id = id + 4 where id = 1;

explain (COSTS OFF) update t2 set id = id + 4 where id > 1;
update t2 set id = id + 4 where id > 1;

explain (COSTS OFF) delete from t2 where id = 1;
delete from t2 where id = 1;

explain (COSTS OFF) delete from t2 where id > 1;
delete from t2 where id > 1;

-- Before update trigger test.

alter table t2 add column count int;

insert into t2 values (1, 'John', 0);

CREATE OR REPLACE FUNCTION update_count() RETURNS trigger LANGUAGE plpgsql AS
$func$
BEGIN
   NEW.count := NEW.count+1;
   RETURN NEW;
END
$func$;

CREATE TRIGGER update_count_trig BEFORE UPDATE ON t2 FOR ROW EXECUTE PROCEDURE update_count();

update t2 set name = 'Jane' where id = 1;

select * from t2;

-- Insert with on conflict
insert into t2 values (1, 'foo') on conflict ON CONSTRAINT t2_pkey do update set id = t2.id+1;

select * from t2;

-- Batched nested loop join (YB_TODO: if I move it below pushdown test, the test fails)

CREATE TABLE p1 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;

CREATE TABLE p2 (a int, b int, c varchar, primary key(a,b));
INSERT INTO p2 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;

SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;

SET yb_bnl_batch_size = 3;

EXPLAIN (COSTS OFF) SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;
-- YB_TODO: Explain has a missing line Index Cond: (a = ANY (ARRAY[t1.a, $1, $2])) under Index Scan
SELECT * FROM p1 t1 JOIN p2 t2 ON t1.a = t2.a WHERE t1.a <= 100 AND t2.a <= 100;

-- Update pushdown test.

CREATE TABLE single_row_decimal (k int PRIMARY KEY, v1 decimal, v2 decimal(10,2), v3 int);
CREATE FUNCTION next_v3(int) returns int language sql as $$
  SELECT v3 + 1 FROM single_row_decimal WHERE k = $1;
$$;

INSERT INTO single_row_decimal(k, v1, v2, v3) values (1,1.5,1.5,1), (2,2.5,2.5,2), (3,null, null,null);
SELECT * FROM single_row_decimal ORDER BY k;
UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = v3 + 1 WHERE k = 1;
-- v2 should be rounded to 2 decimals.
SELECT * FROM single_row_decimal ORDER BY k;

UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = 3 WHERE k = 1;
SELECT * FROM single_row_decimal ORDER BY k;
UPDATE single_row_decimal SET v1 = v1 + 1.555, v2 = v2 + 1.555, v3 = next_v3(1) WHERE k = 1;
SELECT * FROM single_row_decimal ORDER BY k;

-- Delete with returning
insert into t2 values (4), (5), (6);
delete from t2 where id > 2 returning id, name;

-- YB_TODO: There's some issue with drop table
