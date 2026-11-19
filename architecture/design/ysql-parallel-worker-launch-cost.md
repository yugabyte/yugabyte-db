# Costing parallel worker launch in YSQL

## Introduction

Launching a parallel worker costs far more in YugabyteDB than in PostgreSQL,
and the cost grows with the number of relations the worker's plan opens. A
PostgreSQL worker forks from the postmaster, attaches to shared memory and
reads catalog data from local files that the leader has usually warmed in
shared buffers. A YSQL worker also initializes pggate and starts with empty
catalog caches, and the YSQL catalog lives in the master's sys catalog tablet,
so every relcache entry the worker builds is a burst of catalog reads.

Upstream models worker launch as one `parallel_setup_cost` per Gather. The
measurements below find three delays that a flat charge cannot follow: one per
relation in the worker's plan, one per worker, and one per worker per relation.
The model charges the first. Without the per-relation charge the planner picks
a 2-worker Parallel Append for the 600-partition `count(*)` of #36424, because
the scan cost is divided among the workers while nothing on the startup side
grows, and on the three-node cluster measured below that plan ran 15 times
slower than the serial plan.

Catalog preloading replaces the per-relation delay with a fixed delay several
times larger, which the model does not charge. For the one-table LIMIT query of
#36851 the planner picks a 2-worker plan, and on the single-node cluster
measured below, with preloading on, launching the two workers took 131 ms
against 106 ms for the whole serial plan.

This document records what the model charges, the measurements behind the
charge, where the model departs from the measurements, and how to redo them.

## The model

Both `cost_gather()` and `cost_gather_merge()` in
`src/postgres/src/backend/optimizer/path/costsize.c` charge:

```
startup_cost += parallel_setup_cost
             +  yb_parallel_setup_cost_per_relation * relations
```

`parallel_setup_cost` is upstream's GUC with a YB default of 4080
(`YB_DEFAULT_PARALLEL_SETUP_COST`). `yb_parallel_setup_cost_per_relation` is a
YB GUC with a default of 120 (`YB_DEFAULT_PARALLEL_SETUP_COST_PER_RELATION`),
which is six catalog reads at the default `yb_local_roundtrip_cost` of 20. Both
defaults are in `src/postgres/src/include/optimizer/cost.h`.

`relations` is the number of base relations scanned under the Gather, counted
by `yb_count_scan_relations()` on the Gather's subpath:

- A scan of a table counts as one relation whatever the scan type, because a
  worker primes a table and the index it scans together.
- An Append or Merge Append counts the sum of its children, so a partitioned
  table counts once for each partition left after plan-time pruning.
- A join counts both of its inputs.
- An aggregate, group, sort, incremental sort, limit, material, memoize,
  projection, project set, unique or subquery scan path counts what its input
  counts. Any other path type counts nothing.

The charge does not depend on the worker count. The workers prime their caches
concurrently, so the delay before the first tuple grows less than in proportion
to the worker count, and the part that does grow with the worker count is not
charged (Where the model departs from the measurements).

Setting `yb_parallel_setup_cost_per_relation` to 0 removes the charge. The
parallel regress tests that set `parallel_setup_cost` to 0 to force a parallel
plan set this GUC to 0 as well. Because the charge is fixed for a given plan,
a binary without the GUC reproduces it for a plan with a single Gather by
setting `parallel_setup_cost` to `4080 + 120 * relations`.

The cost is charged in `startup_cost` because startup cost is neither divided
by the parallel divisor nor reduced by LIMIT interpolation, and a rescan
charges it again: the default branch of `cost_rescan()` returns the path's
startup cost as the rescan startup cost. That matches the executor, where
every rescan of a Gather shuts its workers down and launches new ones.

## Why a worker pays per relation

A parallel worker is a new backend, so its relcache is empty. Before its first
fetch the worker initializes every scan node in its copy of the plan, and
opening a relation builds a relcache entry from catalog rows the worker has to
read. `EXPLAIN (ANALYZE, DIST)` reports these as Catalog Read Requests.

For an index scan on the primary key of each of R partitions, a worker makes
22 + 6 R catalog reads: six for each table with its primary key, and 22 that do
not depend on R. A one-relation plan makes 25 to 30 depending on the query, and
a scan through a secondary index made the same 27 as a scan through the primary
key.

A worker opens every relation under a Parallel Append even though it scans only
a share of them: the count per worker is the same at every worker count from 1
to 12. The leader's cache is warm after the first execution in a session, so a
serial plan makes no catalog reads.

## Measurements

Taken 2026-11-13 in three environments:

| environment | nodes | hardware | build |
| --- | --- | --- | --- |
| three-node | 3, RF3, one AWS availability zone | m6i.4xlarge (16 vCPU), AlmaLinux 9.5 | `11848c2452a46160d47da9f8f40d4f757ce398fd` |
| single-node | 1, RF1, same zone | m6i.4xlarge | `11848c2452a46160d47da9f8f40d4f757ce398fd` |
| laptop | 1, RF1 | Apple M5 Pro (18 cores), macOS 26.7 | `01462d4e3d10038a2d6f69099f9531e832a54365` |

Both builds are master without this change, and the first build is the second
plus 14 commits that touch neither the planner nor worker startup. The two AWS
clusters ran with TLS on for node-to-node and client-to-node traffic and
`pg_client_use_shared_memory=true`. The laptop ran without TLS and with
`pg_client_use_shared_memory=false`. `max_worker_processes` and
`max_parallel_workers` were raised to 32 everywhere, because at the default of
8 a sweep past 8 workers silently launches fewer workers than planned. The
catalog preload gflags were at their defaults for the first pass.

Method: a colocated database holding `pt600`, a table range-partitioned on its
primary key into 600 partitions of 100 rows, analyzed. The query is
`select count(*) from pt600 where k < N`, with N chosen so that plan-time
pruning leaves 1, 10, 50, 100, 200, 400 or 600 partitions. Under
`yb_test_force_parallel = force` and `max_parallel_workers_per_gather = w` the
plan is a Gather with exactly w workers over a Parallel Append of one Parallel
Index Scan per partition, with the aggregate pushed down. Each cell is
`EXPLAIN (ANALYZE, DIST, SUMMARY ON, FORMAT JSON)` run eight times in one
session, so the leader's cache stays warm, and the median of the last six.
Worker counts were 0 (serial), 1, 2, 4, 8 and 12. The coefficient of variation
was under 12% in every cell on the three-node cluster and under 3% in most.

Execution time in milliseconds on the three-node cluster, from a node that does
not host the master leader:

| relations | serial | 1 | 2 | 4 | 8 | 12 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.3 | 29.2 | 29.8 | 31.4 | 38.0 | 49.5 |
| 10 | 1.5 | 46.4 | 48.2 | 52.5 | 60.8 | 71.0 |
| 50 | 7.1 | 129.4 | 131.9 | 143.3 | 167.3 | 186.6 |
| 100 | 14.9 | 229.2 | 241.4 | 259.0 | 297.4 | 335.9 |
| 200 | 28.4 | 439.7 | 451.5 | 489.7 | 562.2 | 626.4 |
| 400 | 62.6 | 852.8 | 872.5 | 956.9 | 1097.3 | 1222.1 |
| 600 | 92.2 | 1273.3 | 1313.1 | 1435.2 | 1643.2 | 1817.6 |

A least-squares fit of the parallel cells to
`base + a * workers + b * relations + c * workers * relations`, in
milliseconds:

| environment | base | a | b | c | mean error |
| --- | ---: | ---: | ---: | ---: | ---: |
| three-node, session away from the master leader | 22.2 | 1.42 | 2.00 | 0.082 | 2.1% |
| three-node, session on the master leader's node | 23.9 | 1.38 | 1.80 | 0.112 | 1.3% |
| single-node | 24.1 | 1.47 | 1.83 | 0.114 | 2.3% |
| laptop | 15.7 | 1.27 | 0.77 | 0.155 | 6.3% |

The serial scan itself takes 0.15 ms per partition on the AWS clusters, so at
most that much of `b` is scan work.

The query of #36424, `select count(*) from pt600` with every setting at its
default, plans a 2-worker Parallel Append on these builds:

| environment | serial | default plan | ratio |
| --- | ---: | ---: | ---: |
| three-node | 82 ms | 1,289 ms | 15.6 |
| single-node | 85 ms | 1,232 ms | 14.5 |
| laptop | 55 ms | 604 ms | 10.9 |

### What the measurements establish

**The catalog read count is exact and the same everywhere.** Every worker made
22 + 6 R reads in all three environments and at every worker count. The count
is a property of the plan. Only the time each read takes varies.

**The delay is linear in the relation count.** At a fixed worker count a
straight line in R fits every environment with R-squared above 0.998. A table
with 100 partitions and the 600-partition table pruned to 100 took the same
time, 240 ms and 241 ms at 2 workers, so partitions pruned at plan time cost
a worker nothing.

**Catalog reads are the delay once a plan has many relations.** On the
three-node cluster at one worker, the reads took 7.8 ms of 29.2 ms for one
relation and 974 ms of 1,273 ms for 600. A one-relation probe therefore
understates how much of the launch is catalog work.

**The delay is processing time per read, not distance to the master.** A read
took 0.24 ms with the master on the session's node and 0.27 ms with the master
leader on another node in the zone, and the single-node cluster's `b` is within
10% of the three-node cluster's. The laptop's 0.13 ms per read comes with both
faster cores and no TLS, and the measurements do not separate the two.

**Workers slow each other down.** The time per read rose from 0.27 ms at one
worker to 0.35 ms at 12 on the three-node cluster, and from 0.13 ms to 0.36 ms
on the laptop. A fit without the `c` term misses by 45% to 134% on average,
against 1% to 6% with it.

**The per-worker part matches the earlier one-relation probe.** At one relation
the fit in the worker count is 25.8 ms + 1.82 ms per worker. A probe on
2026-09-16, on a three-node cluster of the same instance type, gave 29.3 ms +
1.68 ms per worker.

**The charge moves the #36424 query to its serial plan and leaves a
10-partition scan parallel.** The planner costs were identical in all three
environments: 16,209.81 serial and 12,183.11 for the 2-worker plan of the
#36424 query. Raising `parallel_setup_cost` by `120 * 600` made the planner
choose the serial plan at every worker count from 1 to 8. For a table of 10
partitions and 200,000 rows, raising `parallel_setup_cost` by `120 * 10` left
the 2-worker plan in place, at 12,419.92 against 15,970.50 serial.

### Catalog preloading

With the tserver gflag `ysql_catalog_preload_additional_tables` set to true,
a new backend preloads additional catalog tables at startup. A second pass
measured the same sweep with the flag on:

| environment | base | a | b | c | mean error |
| --- | ---: | ---: | ---: | ---: | ---: |
| three-node, session away from the master leader | 129.9 | 11.9 | 0.23 | 0.022 | 5.5% |
| three-node, session on the master leader's node | 123.4 | 12.4 | 0.30 | 0.016 | 6.2% |
| single-node | 129.8 | 12.3 | 0.21 | 0.021 | 5.0% |
| laptop | 78.3 | 12.0 | 0.10 | 0.012 | 2.0% |

Every worker made 13 catalog reads whatever the relation count. The time per
relation fell from about 2 ms to 0.2 to 0.3 ms on the AWS clusters, which
includes the scan itself, while `base` grew about five times and `a` about
eight times. The fit is rougher than in the first pass because the time grows
faster than linearly past 8 workers.

| query, 2 workers | environment | serial | flag off | flag on |
| --- | --- | ---: | ---: | ---: |
| `count(*)`, 600 partitions | three-node | 82 ms | 1,289 ms | 289 ms |
| `count(*)`, 600 partitions | laptop | 55 ms | 604 ms | 159 ms |
| `count(*)`, 10 partitions, 200,000 rows | three-node | 42 ms | 53 ms | 163 ms |
| `count(*)`, 10 partitions, 200,000 rows | laptop | 23 ms | 25 ms | 103 ms |

At 2 workers the two settings cross at about 70 relations on the AWS clusters
and about 90 on the laptop. Below that, preloading makes a parallel plan slower
to start. Serial times are from the first pass, and the second pass's were
within 4 ms of them.

The fixed delay is a property of the database and of what is preloaded, not of
the plan. A backend preloads the catalog at startup when
`ysql_catalog_preload_additional_tables` is true or
`ysql_catalog_preload_additional_table_list` is not empty. The preload
fetches 18 catalog tables whole, among them `pg_class`, `pg_attribute`,
`pg_index`, `pg_constraint` and `pg_type`, together with the tables the list
names. `YBLoadRelations()` then builds a relcache entry for every relation in
the database. The boolean gflag stands for a list of eight tables, one of
which is `pg_proc`. The sweep above ran in a database of 714 tables.

A further run on the three-node cluster on 2026-11-16 measured a one-table
parallel `count(*)` at one worker, from a node that does not host the master
leader, under five settings of the two gflags. With only the scanned table in
the database:

| preload gflags | execution time |
| --- | ---: |
| neither | 25 to 26 ms |
| list naming `pg_statistic` | 68 ms |
| list naming `pg_proc` | 131 to 132 ms |
| boolean | 136 ms |
| boolean, and list naming `pg_statistic` | 133 to 136 ms |

Other databases added one kind of object to the one-table database:

| added to the database | added delay | under |
| --- | ---: | --- |
| 1,000 two-column tables, each with a primary key | 28 to 31 ms | all four preload settings |
| 3,000 such tables | 90 to 94 ms | all four preload settings |
| 500 tables of 41 columns | 48 to 49 ms | all four preload settings |
| 3,000 schemas | under 4 ms | all four preload settings |
| 8,159 `pg_statistic` rows, 3.8 MB, on top of the 41-column tables | 10 to 13 ms | both settings whose list names `pg_statistic` |
| 1,000 SQL functions | 46 to 47 ms | list naming `pg_proc` |
| 3,000 SQL functions | 200 to 206 ms | list naming `pg_proc`, and both settings with the boolean |
| 6,000 SQL functions | 567 to 573 ms | list naming `pg_proc` |

An addition changed nothing measurable where the setting does not preload the
table the addition goes into: functions under the list naming `pg_statistic`,
and statistics under the boolean alone or the list naming `pg_proc`. With
neither gflag set, every database took 25 to 30 ms with the same 27 catalog
reads, and so did a scan of one of the 41-column tables.

The delay is linear in the tables and faster than linear in the functions.
`YbPreloadCatalogCache()` groups the `pg_proc` rows into one list per function
name, and finds the list for a row by comparing the row's name with the lists
made so far, one after another. The comparisons therefore grow with the square
of the number of distinct function names. Counted for `pg_proc` rows taken in
OID order, a database with no user functions needs 4.5 million comparisons and
the three additions need 3.2, 12.7 and 34.5 million more. If the comparisons
are the whole of the functions' delay, one comparison takes 14 to 17 ns; the
delay was not profiled. At 14 ns the 4.5 million comparisons take 63 ms, and
in the one-table database the list naming `pg_proc` took 62 to 64 ms longer
than the list naming `pg_statistic`.

Both tables are at one worker. Repeat runs differed by up to 7 ms at one
worker, 15 ms at four and 39 ms at eight, so the additions were not compared
across worker counts. On the laptop, measured on 2026-11-14 with the boolean
gflag, the one-table database took 79 ms, 1,000 tables added 19 ms and 3,000
functions added 77 ms.

### A one-relation plan under a LIMIT

Measured 2026-11-18 with the table and a query of #36851: `ord`, 3,000,000
rows with a range primary key split into three tablets, not colocated, and

```
SELECT id, k2, status, amt FROM ord WHERE k2 = 500 ORDER BY id DESC LIMIT 200
```

One row in 1,000 matches `k2 = 500`. The serial plan is a backward scan of the
primary key with the LIMIT pushed down, and it reads 199,500 rows in one
request. The planner costs it at 26,317 against 16,218 for the 2-worker plan
and 11,603 for the 8-worker plan, a Gather Merge over a Parallel Index Scan
Backward, within 0.2% in all three environments.

The three-node cluster had the preload gflags at their defaults and the
single-node cluster had `ysql_catalog_preload_additional_tables` set to true.
The laptop ran build `0b04a0500215fa8662f97f4f7e6be050e6a5c19d` with the
preload gflags and `max_worker_processes` at their defaults, so it launched 6
of 8 planned workers. Each figure is the median of five or six runs in one
session after a discarded first run.

The launch alone was measured with the same query restricted to
`id >= 2999000`, a range of 1,000 rows that its serial plan scans in 1 ms or
less, under `yb_test_force_parallel = force`. A Gather Merge returns its first
row only after every worker has delivered a row or finished, so the time to
the first row is the launch. In milliseconds:

| workers planned | three-node, preload off | single-node, preload on | laptop, preload off |
| ---: | ---: | ---: | ---: |
| 1 | 27.7 | 129.6 | 15.1 |
| 2 | 27.8 | 130.8 | 14.8 |
| 4 | 29.9 | 156.8 | 18.7 |
| 8 | 35.6 | 174.9 | 24.2 |

A worker made 30 catalog reads with preloading off and 13 with it on. For the
query itself the counts were 26 and 10.

The query itself, in milliseconds:

| plan | three-node, preload off | single-node, preload on | laptop, preload off |
| --- | ---: | ---: | ---: |
| serial | 112 | 106 | 56 |
| 2 workers, first row | 142 | 243 | 71 |
| 2 workers, total | 180 | 282 | 90 |
| 8 workers, total | 210 | 369 | 109 |

**With preloading off the flat charge is about the launch.** The serial plan's
time is proportional to its scan's startup cost for LIMIT 10 to LIMIT 1,000, at
122 to 129 cost units per millisecond on the three-node cluster, 128 to 132 on
the single-node cluster and 234 to 252 on the laptop. At 127 and 245 units per
millisecond the launch is 3,500 to 4,500 units on the three-node cluster and
3,600 to 5,900 on the laptop, against 4,200 charged for one relation.

**With preloading on the launch is four to five times the charge.** 130 to
175 ms on the single-node cluster is 16,700 to 22,600 units at 129 units per
millisecond. On the laptop
build the planner chose the serial plan with `parallel_setup_cost` at 14,124
for 2 workers and at 18,729 for 8, and the parallel plan at 9,000 and 14,126,
so charging the measured launch would give the serial plan at both worker
counts.

**The cluster of #36851 shows the preloading count.** The EXPLAIN output in the
issue has 47 catalog reads with 2 workers launched and 87 with 6, which is 10
for each added worker, the count measured here with preloading on. The issue
does not state the cluster's gflags.

**Launch is not all of the gap.** With preloading off the 2-worker plan still
takes 1.6 times the serial plan. A worker's rows reach the leader only when
16 kB of them are queued or the worker finishes, and a worker fetches three
`yb_parallel_range_size` ranges before it has its 200 rows, so the first row
arrives one launch and three range fetches after the start; the leader then
fetches one more range of its own. On the cost side, the Limit node adds a
prorated share of the scan's run cost to a serial plan whose startup cost
already covers the LIMIT's rows, which nearly doubles the serial plan's cost.
None of these is a launch cost, and this model addresses none of them.

### Where the model departs from the measurements

At the cost model's convention of roughly 300 cost units per millisecond, on
m6i.4xlarge:

| | flat | per worker | per relation | per worker per relation |
| --- | ---: | ---: | ---: | ---: |
| charged | 4,080 | 0 | 120 | 0 |
| measured, preload flag off | 6,900 | 430 | 570 | 30 |
| measured, preload flag on | 39,000 | 3,700 | 70 | 6 |

The measured columns are the fit coefficients, so up to 45 units of each
per-relation figure is scan work.

The conversion is not one number. On the three-node cluster on 2026-11-18,
serial scans of `ord` ran at 122 to 324 cost units per millisecond depending on
the scan type and on how many rows the scan returned, and the serial plan of
the #36851 query at 122 to 129. At 127 the measured rows of the table are 42%
of the figures shown.

- **The per-relation default is a lower bound.** 120 prices the six reads at
  the local round-trip cost. The measured delay is 1.8 to 2.0 ms per relation,
  about five times that at 300 units per millisecond and about twice that at
  127.
- **Nothing is charged per worker.** Neither the 1.4 ms per worker nor the
  0.08 to 0.11 ms per worker per relation enters the cost, so adding workers
  still lowers the cost of a plan whose launch gets slower.
- **The flat part undercharges at 300 units per millisecond.** The
  10-partition query above stays parallel with the charge and runs in 53 ms
  against 42 ms serial. At the ratio of the #36851 query's serial plan the
  flat part is about the launch of a one-relation plan (A one-relation plan
  under a LIMIT).
- **With catalog preloading on, the shape is wrong.** The per-relation charge
  is above the delay it prices. The flat part is a tenth of the fixed delay
  measured in the sweep's database at 300 units per millisecond and a quarter
  to a fifth of a one-relation launch at the #36851 query's ratio, and the
  fixed delay grows with the tables in the database and, faster than linearly,
  with the functions. The model reads neither preload gflag.
- **Partitions pruned at executor startup are counted.** The count is taken
  after plan-time pruning only. What a worker reads for a plan that prunes at
  executor startup was not measured.
- **One zone only.** All nodes of the three-node cluster were in one
  availability zone. A master leader in another zone or region was not
  measured.

## When to recalibrate

Recalibrate when any of the following changes, because each one moves a term
the defaults absorb:

- **A PostgreSQL major version upgrade.** Catalog contents and layout change,
  so the reads per relation and per worker change directly.
- **Catalog preloading**, including the
  `ysql_catalog_preload_additional_tables`,
  `ysql_catalog_preload_additional_table_list`,
  `ysql_minimal_catalog_caches_preload` and `ysql_enable_read_request_caching`
  gflags, or any change to which catalogs a backend loads at startup. Only the
  first two were measured.
- **How a relcache entry is built**, including any change that lets a worker
  reuse entries the leader already has.
- **Worker startup or pggate initialization**, including how a worker attaches
  to shared memory.
- **The connection manager**, if parallel workers ever come from a pool rather
  than from new backends. That would remove the premise of the model, not just
  change its constants.
- **The units-per-millisecond convention** of the cost model, since the
  defaults are expressed in it, and any change to scan costing that moves the
  ratio for the serial plans a parallel plan competes with.

## How to recalibrate

1. Create a cluster and set `max_worker_processes` and `max_parallel_workers`
   well above the highest worker count in the sweep, through the
   `ysql_pg_conf_csv` tserver gflag.
2. Create a table partitioned into several hundred small partitions and
   analyze it. Choose predicates that leave a range of partition counts after
   plan-time pruning.
3. Connect to a node that does not host the master leader. In one session, for
   each partition count and each worker count, run
   `EXPLAIN (ANALYZE, DIST, SUMMARY ON, FORMAT JSON)` of an aggregate under
   `yb_test_force_parallel = force` with `max_parallel_workers_per_gather` set
   to the worker count.
4. Take at least eight repetitions for each cell and discard the first two.
   A coefficient of variation above roughly 10% means the cluster is too noisy
   or short of CPU at that worker count.
5. Confirm that Workers Launched equals Workers Planned in every sample, and
   that Catalog Read Requests divided by the worker count is the same at every
   worker count.
6. Fit `base + a * workers + b * relations + c * workers * relations` to the
   execution times. If the mean error is above a few percent, another term is
   scaling and the shape is no longer right.
7. Measure the cost units per millisecond of serial plans on the same cluster,
   for example from an ordered scan with a storage filter under a range of
   LIMIT values, whose time is proportional to its scan's startup cost.
8. Convert `b` to cost units at that ratio and update
   `YB_DEFAULT_PARALLEL_SETUP_COST_PER_RELATION`.

Changing the default moves plan choice for every query that scans more than a
few relations in parallel, so the parallel and partition regress suites need
their expected output reviewed after a change.

## Future direction

The catalog reads are arguably a defect to fix and not a cost to model. Handing
a worker the leader's relcache entries, or reusing warm workers across Gather
launches, would remove the per-relation delay without the fixed delay that
preloading adds.

Until then, three additions would bring the model to the measured shape: a
charge per worker, a charge per worker per relation, and a separate charge
when catalog preloading is on. Each one changes plan choice broadly and needs
its own validation.

The charge for catalog preloading comes first, because it decides plans of any
size: with preloading on, the launch of a one-relation plan took four to five
times what the model charges, enough to change the plan of the #36851 query.
Constants cannot make up that charge, because under preloading the fixed delay
follows the tables and functions in the database and the catalog tables the
gflags name. `YbCatalogPreloadRequired()` tells whether a new backend
preloads, but the size of the delay has to be measured in the database.

The planner is not the place to measure the delay. A prototype charged the
time the planning backend's own preload had taken. The prototype gave the
#36851 query its serial plan with preloading on and changed no cost with
preloading off, but the planning backend's preload is not a worker's: on the
laptop on 2026-11-18 a client backend preloaded in 213 to 265 ms and a worker
in 62 to 71 ms. A client backend's preload time also differs from one
connection to the next, so the same query got different costs in different
sessions.

The preload charge therefore needs one value for each database, the time a
worker's preload takes there, supplied from outside the planner. How it is
supplied is open. A setting can hold the value, as `parallel_setup_cost` can
already be set for one database with `ALTER DATABASE ... SET`, or a step
outside the planner can measure the value on launched workers and store it.

The charges without preloading need no value from the cluster. At one worker a
catalog read took 0.27 ms on the three-node cluster and 0.13 ms on the laptop,
and the #36851 query's serial plan ran at 127 and 245 cost units per
millisecond, so a read cost about 34 units on the one and about 32 on the
other. A master leader in another zone or region was not measured.
