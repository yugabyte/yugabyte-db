--
-- PARALLEL
--

-- TODO as of GHI 20637 some of tests below do not use parallel query, since
-- it is currently restricted to colocated tables. Eventually we will enable
-- it for distributed tables and update expected results.

create function sp_parallel_restricted(int) returns int as
  $$begin return $1; end$$ language plpgsql parallel restricted;

-- enable parallel query for YB tables
set yb_parallel_range_rows  to 1;
set yb_enable_base_scans_cost_model to true;

-- encourage use of parallel plans
set parallel_setup_cost=0;
set parallel_tuple_cost=0;
set min_parallel_table_scan_size=0;
set max_parallel_workers_per_gather=4;

-- Parallel Append is not to be used when the subpath depends on the outer param
create table part_pa_test(a int, b int) partition by range(a);
create table part_pa_test_p1 partition of part_pa_test for values from (minvalue) to (0);
create table part_pa_test_p2 partition of part_pa_test for values from (0) to (maxvalue);
explain (costs off)
	select (select max((select pa1.b from part_pa_test pa1 where pa1.a = pa2.a)))
	from part_pa_test pa2;
drop table part_pa_test;

-- test with leader participation disabled
set parallel_leader_participation = off;
explain (costs off)
  select count(*) from tenk1 where stringu1 = 'GRAAAA';
select count(*) from tenk1 where stringu1 = 'GRAAAA';

-- test with leader participation disabled, but no workers available (so
-- the leader will have to run the plan despite the setting)
set max_parallel_workers = 0;
explain (costs off)
  select count(*) from tenk1 where stringu1 = 'GRAAAA';
select count(*) from tenk1 where stringu1 = 'GRAAAA';

reset max_parallel_workers;
reset parallel_leader_participation;

-- test that parallel_restricted function doesn't run in worker
explain (verbose, costs off)
select sp_parallel_restricted(unique1) from tenk1
  where stringu1 = 'GRAAAA' order by 1;

-- test parallel plan when group by expression is in target list.
explain (costs off)
	select length(stringu1) from tenk1 group by length(stringu1);
select length(stringu1) from tenk1 group by length(stringu1);

explain (costs off)
	select stringu1, count(*) from tenk1 group by stringu1 order by stringu1;

-- test that parallel plan for aggregates is not selected when
-- target list contains parallel restricted clause.
explain (costs off)
	select  sum(sp_parallel_restricted(unique1)) from tenk1
	group by(sp_parallel_restricted(unique1));

-- test prepared statement
prepare tenk1_count(integer) As select  count((unique1)) from tenk1 where hundred > $1;
explain (costs off) execute tenk1_count(1);
execute tenk1_count(1);
deallocate tenk1_count;

-- test parallel plans for queries containing un-correlated subplans.
explain (costs off)
	select count(*) from tenk1 where (two, four) not in
	(select hundred, thousand from tenk2 where thousand > 100);
select count(*) from tenk1 where (two, four) not in
	(select hundred, thousand from tenk2 where thousand > 100);
-- this is not parallel-safe due to use of random() within SubLink's testexpr:
explain (costs off)
	select * from tenk1 where (unique1 + random())::integer not in
	(select ten from tenk2);

-- test parallel plan for a query containing initplan.
set enable_indexscan = off;
set enable_indexonlyscan = off;
set enable_bitmapscan = off;

explain (costs off)
	select count(*) from tenk1
        where tenk1.unique1 = (Select max(tenk2.unique1) from tenk2);
select count(*) from tenk1
    where tenk1.unique1 = (Select max(tenk2.unique1) from tenk2);

reset enable_indexscan;
reset enable_indexonlyscan;
reset enable_bitmapscan;

-- test parallel index scans.
set enable_seqscan to off;
set enable_bitmapscan to off;

explain (costs off)
	select  count((unique1)) from tenk1 where hundred > 1;
select  count((unique1)) from tenk1 where hundred > 1;

-- test parallel index-only scans.
explain (costs off)
	select  count(*) from tenk1 where thousand > 95;
select  count(*) from tenk1 where thousand > 95;

-- test rescan cases too
set enable_material = false;

explain (costs off)
select * from
  (select count(unique1) from tenk1 where hundred > 10) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (select count(unique1) from tenk1 where hundred > 10) ss
  right join (values (1),(2),(3)) v(x) on true;

explain (costs off)
select * from
  (select count(*) from tenk1 where thousand > 99) ss
  right join (values (1),(2),(3)) v(x) on true;
select * from
  (select count(*) from tenk1 where thousand > 99) ss
  right join (values (1),(2),(3)) v(x) on true;

reset enable_material;
reset enable_seqscan;
reset enable_bitmapscan;

-- test parallel merge join path.
set enable_hashjoin to off;
set enable_nestloop to off;

explain (costs off)
	select  count(*) from tenk1, tenk2 where tenk1.unique1 = tenk2.unique1;
select  count(*) from tenk1, tenk2 where tenk1.unique1 = tenk2.unique1;

reset enable_hashjoin;
reset enable_nestloop;

-- test gather merge
set enable_hashagg = false;

explain (costs off)
   select count(*) from tenk1 group by twenty;

select count(*) from tenk1 group by twenty;

--test expressions in targetlist are pushed down for gather merge
create function sp_simple_func(var1 integer) returns integer
as $$
begin
        return var1 + 10;
end;
$$ language plpgsql PARALLEL SAFE;

explain (costs off, verbose)
    select ten, sp_simple_func(ten) from tenk1 where ten < 100 order by ten;

drop function sp_simple_func(integer);

-- test handling of SRFs in targetlist (bug in 10.0)

explain (costs off)
   select count(*), generate_series(1,2) from tenk1 group by twenty;

select count(*), generate_series(1,2) from tenk1 group by twenty;

-- test gather merge with parallel leader participation disabled
set parallel_leader_participation = off;

explain (costs off)
   select count(*) from tenk1 group by twenty;

select count(*) from tenk1 group by twenty;

reset parallel_leader_participation;

--test rescan behavior of gather merge
set enable_material = false;

explain (costs off)
select * from
  (select string4, count(unique2)
   from tenk1 group by string4 order by string4) ss
  right join (values (1),(2),(3)) v(x) on true;

select * from
  (select string4, count(unique2)
   from tenk1 group by string4 order by string4) ss
  right join (values (1),(2),(3)) v(x) on true;

reset enable_material;

reset enable_hashagg;

-- check parallelized int8 aggregate (bug #14897)
explain (costs off)
select avg(unique1::int8) from tenk1;

select avg(unique1::int8) from tenk1;

-- gather merge test with a LIMIT
explain (costs off)
  select fivethous from tenk1 order by fivethous limit 4;

select fivethous from tenk1 order by fivethous limit 4;

-- gather merge test with 0 worker
set max_parallel_workers = 0;
explain (costs off)
   select string4 from tenk1 order by string4 limit 5;
select string4 from tenk1 order by string4 limit 5;

-- gather merge test with 0 workers, with parallel leader
-- participation disabled (the leader will have to run the plan
-- despite the setting)
set parallel_leader_participation = off;
explain (costs off)
   select string4 from tenk1 order by string4 limit 5;
select string4 from tenk1 order by string4 limit 5;

reset parallel_leader_participation;
reset max_parallel_workers;

BEGIN;

-- exercise record typmod remapping between backends
CREATE FUNCTION make_record(n int)
  RETURNS RECORD LANGUAGE plpgsql PARALLEL SAFE AS
$$
BEGIN
  RETURN CASE n
           WHEN 1 THEN ROW(1)
           WHEN 2 THEN ROW(1, 2)
           WHEN 3 THEN ROW(1, 2, 3)
           WHEN 4 THEN ROW(1, 2, 3, 4)
           ELSE ROW(1, 2, 3, 4, 5)
         END;
END;
$$;
SAVEPOINT settings;
SET LOCAL force_parallel_mode = 1;
SELECT make_record(x) FROM (SELECT generate_series(1, 5) x) ss ORDER BY x;
ROLLBACK TO SAVEPOINT settings;
DROP function make_record(n int);

-- test the sanity of parallel query after the active role is dropped.
drop role if exists regress_parallel_worker;
create role regress_parallel_worker;
set role regress_parallel_worker;
reset session authorization;
drop role regress_parallel_worker;
set force_parallel_mode = 1;
select count(*) from tenk1;
reset force_parallel_mode;
reset role;

-- Window function calculation can't be pushed to workers.
explain (costs off, verbose)
  select count(*) from tenk1 a where (unique1, two) in
    (select unique1, row_number() over() from tenk1 b);


-- LIMIT/OFFSET within sub-selects can't be pushed to workers.
explain (costs off)
  select * from tenk1 a where two in
    (select two from tenk1 b where stringu1 like '%AAAA' limit 3);

-- to increase the parallel query test coverage
SAVEPOINT settings;
SET LOCAL force_parallel_mode = 1;
EXPLAIN (analyze, timing off, summary off, costs off) SELECT * FROM tenk1;
ROLLBACK TO SAVEPOINT settings;

-- provoke error in worker
-- (make the error message long enough to require multiple bufferloads)
SAVEPOINT settings;
SET LOCAL force_parallel_mode = 1;
select (stringu1 || repeat('abcd', 5000))::int2 from tenk1 where unique1 = 1;
ROLLBACK TO SAVEPOINT settings;

-- test interaction with set-returning functions
SAVEPOINT settings;

-- multiple subqueries under a single Gather node
-- must set parallel_setup_cost > 0 to discourage multiple Gather nodes
SET LOCAL parallel_setup_cost = 10;
EXPLAIN (COSTS OFF)
SELECT unique1 FROM tenk1 WHERE fivethous = tenthous + 1
UNION ALL
SELECT unique1 FROM tenk1 WHERE fivethous = tenthous + 1;
ROLLBACK TO SAVEPOINT settings;

-- can't use multiple subqueries under a single Gather node due to initPlans
EXPLAIN (COSTS OFF)
SELECT unique1 FROM tenk1 WHERE fivethous =
	(SELECT unique1 FROM tenk1 WHERE fivethous = 1 LIMIT 1)
UNION ALL
SELECT unique1 FROM tenk1 WHERE fivethous =
	(SELECT unique2 FROM tenk1 WHERE fivethous = 1 LIMIT 1)
ORDER BY 1;

-- test passing expanded-value representations to workers
CREATE FUNCTION make_some_array(int,int) returns int[] as
$$declare x int[];
  begin
    x[1] := $1;
    x[2] := $2;
    return x;
  end$$ language plpgsql parallel safe;
CREATE TABLE fooarr(f1 text, f2 int[], f3 text);
INSERT INTO fooarr VALUES('1', ARRAY[1,2], 'one');

PREPARE pstmt(text, int[]) AS SELECT * FROM fooarr WHERE f1 = $1 AND f2 = $2;
EXPLAIN (COSTS OFF) EXECUTE pstmt('1', make_some_array(1,2));
EXECUTE pstmt('1', make_some_array(1,2));
DEALLOCATE pstmt;

-- test interaction between subquery and partial_paths
SET LOCAL min_parallel_table_scan_size TO 0;
CREATE VIEW tenk1_vw_sec WITH (security_barrier) AS SELECT * FROM tenk1;
EXPLAIN (COSTS OFF)
SELECT 1 FROM tenk1_vw_sec WHERE EXISTS (SELECT 1 WHERE unique1 = 0);

rollback;

-- GHI 21320
CREATE TABLE c (pk integer NOT NULL, col_varchar_key character varying(1), col_varchar_nokey character varying(1), CONSTRAINT c_pkey PRIMARY KEY(pk ASC));
CREATE TABLE d (pk integer NOT NULL, col_varchar_key character varying(1), col_varchar_nokey character varying(1), CONSTRAINT d_pkey PRIMARY KEY(pk ASC));
CREATE TABLE dummy (i integer);
COPY c (pk, col_varchar_key, col_varchar_nokey) FROM stdin;
1	v	v
2	v	v
3	c	c
4	\N	\N
5	x	x
6	i	i
7	e	e
8	p	p
9	s	s
10	j	j
11	z	z
12	c	c
13	a	a
14	q	q
15	y	y
16	\N	\N
17	r	r
18	v	v
19	\N	\N
20	r	r
\.

COPY d (pk, col_varchar_key, col_varchar_nokey) FROM stdin;
1	c	c
2	c	c
3	q	q
4	g	g
5	e	e
6	l	l
7	\N	\N
8	v	v
9	c	c
10	u	u
11	x	x
12	x	x
13	x	x
14	l	l
15	e	e
16	s	s
17	k	k
18	m	m
19	x	x
20	s	s
21	h	h
22	u	u
23	x	x
24	l	l
25	p	p
26	i	i
27	u	u
28	i	i
29	i	i
30	e	e
31	h	h
32	f	f
33	\N	\N
34	p	p
35	n	n
36	h	h
37	m	m
38	x	x
39	d	d
40	d	d
41	t	t
42	\N	\N
43	\N	\N
44	v	v
45	u	u
46	p	p
47	o	o
48	v	v
49	m	m
50	x	x
51	n	n
52	b	b
53	\N	\N
54	r	r
55	v	v
56	a	a
57	u	u
58	\N	\N
59	b	b
60	s	s
61	t	t
62	b	b
63	m	m
64	v	v
65	n	n
66	j	j
67	\N	\N
68	\N	\N
69	h	h
70	k	k
71	k	k
72	\N	\N
73	n	n
74	e	e
75	s	s
76	w	w
77	y	y
78	z	z
79	b	b
80	f	f
81	s	s
82	d	d
83	\N	\N
84	d	d
85	n	n
86	i	i
87	\N	\N
88	h	h
89	d	d
90	c	c
91	i	i
92	t	t
93	g	g
94	q	q
95	l	l
96	n	n
97	z	z
98	n	n
99	r	r
100	p	p
\.

COPY dummy (i) FROM stdin;
0
\.

/*+ Set(enable_hashjoin off) Set(enable_mergejoin off) Set(enable_material off) */
EXPLAIN (analyze, timing off, summary off, costs off) SELECT
FROM
    D AS table2
    JOIN C AS table3 ON (
        table3.col_varchar_nokey = table2.col_varchar_key
    )
WHERE
    table2.col_varchar_nokey <> ANY (
        SELECT
            'j'
        FROM
            DUMMY
            UNION
            ALL
        SELECT
            'r'
        FROM
            DUMMY
    );
