--
-- insert...on conflict on constraint
--
CREATE TABLE tab (i int PRIMARY KEY, j int CONSTRAINT tab_j_constraint UNIQUE);
CREATE UNIQUE INDEX ON tab (j);
CREATE UNIQUE INDEX tab_idx ON tab (j);
-- Insert some data
INSERT INTO tab VALUES (1, 1);
INSERT INTO tab VALUES (200, 200);
-- Use generated constraint name for primary key.
INSERT INTO tab VALUES (1, 7) ON CONFLICT ON CONSTRAINT tab_pkey DO UPDATE
    SET j = tab.j + excluded.j;
-- Use select to verify result.
SELECT * FROM tab;
-- Use system generated name for unique index.
INSERT INTO tab VALUES (1, 200) ON CONFLICT ON CONSTRAINT tab_j_idx DO UPDATE
    SET j = tab.j + excluded.j;
-- Error: Name of index is not a constraint name.
INSERT INTO tab VALUES (1, 1) ON CONFLICT ON CONSTRAINT tab_idx DO NOTHING;
-- Use conflict for unique column
INSERT INTO tab VALUES (1, 200) ON CONFLICT (j) DO UPDATE
    SET j = tab.j + excluded.j;
-- Use SELECT to verify result.
SELECT * FROM tab;
-- Use conflict for unique constraint - noop
INSERT INTO tab VALUES (1, 400) ON CONFLICT ON CONSTRAINT tab_j_constraint DO NOTHING;
-- Use SELECT to verify result.
SELECT * FROM tab;
-- Use conflict for unique constraint - update
INSERT INTO tab VALUES (1, 400) ON CONFLICT ON CONSTRAINT tab_j_constraint DO UPDATE
    SET j = tab.j + excluded.j;
-- Use SELECT to verify result.
SELECT * FROM tab;

/* Taken from "arrays" test, changing temp table to YB table. */
set enable_seqscan to off;
set enable_bitmapscan to off;

-- test ON CONFLICT DO UPDATE with arrays
create table arr_pk_tbl (pk int4 primary key, f1 int[]);
insert into arr_pk_tbl values (1, '{1,2,3}');
insert into arr_pk_tbl values (1, '{3,4,5}') on conflict (pk)
  do update set f1[1] = excluded.f1[1], f1[3] = excluded.f1[3]
  returning pk, f1;
insert into arr_pk_tbl(pk, f1[1:2]) values (1, '{6,7,8}') on conflict (pk)
  do update set f1[1] = excluded.f1[1],
    f1[2] = excluded.f1[2],
    f1[3] = excluded.f1[3]
  returning pk, f1;

-- note: if above selects don't produce the expected tuple order,
-- then you didn't get an indexscan plan, and something is busted.
reset enable_seqscan;
reset enable_bitmapscan;

/* Taken from "insert_conflict" test, changing YB table to temp table. */
create temp table selfconflict (f1 int primary key, f2 int);

begin transaction isolation level read committed;
insert into selfconflict values (1,1), (1,2) on conflict do nothing;
commit;

begin transaction isolation level repeatable read;
insert into selfconflict values (2,1), (2,2) on conflict do nothing;
commit;

begin transaction isolation level serializable;
insert into selfconflict values (3,1), (3,2) on conflict do nothing;
commit;

begin transaction isolation level read committed;
insert into selfconflict values (4,1), (4,2) on conflict(f1) do update set f2 = 0;
commit;

begin transaction isolation level repeatable read;
insert into selfconflict values (5,1), (5,2) on conflict(f1) do update set f2 = 0;
commit;

begin transaction isolation level serializable;
insert into selfconflict values (6,1), (6,2) on conflict(f1) do update set f2 = 0;
commit;

select * from selfconflict;

drop table selfconflict;
