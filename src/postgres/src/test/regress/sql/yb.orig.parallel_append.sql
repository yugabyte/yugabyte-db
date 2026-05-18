create table t(k bigint primary key, v text);
create table t1(k bigint primary key, v text);
create table t2(k bigint primary key, v text);
create table lp (a char) partition by list (a);
create table lp_default partition of lp default;
create table lp_ef partition of lp for values in ('e', 'f');
create table lp_ad partition of lp for values in ('a', 'd');
create table lp_bc partition of lp for values in ('b', 'c');
create table lp_null partition of lp for values in (null);

insert into t values (2, 'value_t_2'), (3, 'value_t_3');
insert into t1 values (1, 'value_t1_1'), (2, 'value_t1_2');
insert into t2 values (3, 'value_t2_3'), (4, 'value_t2_4');

set yb_enable_base_scans_cost_model to true;
set enable_parallel_append to false;
set parallel_setup_cost to 0;
set parallel_tuple_cost to 0;

-- simple union
explain (costs off)
select * from t1 union select * from t2;
select * from t1 union select * from t2;

-- simple union all
explain (costs off)
select * from t1 union all select * from t2;
select * from t1 union all select * from t2;

-- joins union
explain (costs off)
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;

-- joins union all
explain (costs off)
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;

-- partitioned table
explain (costs off)
select * from lp;
select * from lp;

set enable_parallel_append to true;

-- do not execute those parallel queries, there are known issues with YB PA
explain (costs off)
select * from t1 union select * from t2;

-- simple union all
explain (costs off)
select * from t1 union all select * from t2;

-- joins union
explain (costs off)
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;

-- joins union all
explain (costs off)
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;

-- partitioned table
explain (costs off)
select * from lp;

-- union of non-YB relations (#21733)
explain (costs off)
/*+ Set(enable_seqscan OFF) */ select 'l' UNION ALL (SELECT 'g') ORDER BY 1;

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

drop table c;
drop table d;
drop table dummy;
