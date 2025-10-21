--
-- Legacy mode Join Plans
--

set client_min_messages = 'warning';

drop table if exists t1, t2, t2r;

create table t1 (a int, b int);
insert into t1 select i, i from generate_series(1, 1000) i;

create table t2 (k1 int, k2 int, k3 int, k4 int, k5 int, v int, primary key (k1 hash));
create unique index i_t2_k2 on t2 (k2 hash);
create index i_t2_k3 on t2 (k3 hash);
create unique index i_t2_k4k5 on t2 (k4 hash, k5 asc);
insert into t2 select i, i, i % 10000, i % 10000, (i / 10000) * 10000, i from generate_series(1, 100000) i;

create table t2r (k1 int, k2 int, k3 int, k4 int, k5 int, v int, primary key (k1 asc));
create unique index i_t2r_k2 on t2r (k2 asc);
create index i_t2r_k3 on t2r (k3 asc);
create unique index i_t2r_k4k5 on t2r (k4 asc, k5 asc);
insert into t2r select i, i, i % 10000, i % 10000, (i / 10000) * 10000, i from generate_series(1, 100000) i;


\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/legacy_join_plan_queries.sql'

----------------
-- Unanalyzed --
----------------

--
-- Pre-2024.1 behavior (20.2.x and earlier)
--

set yb_enable_cbo = legacy_bnl_mode;

-- Pre-2024.1 defaults
set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;
\i :filename

-- BNL enabled
set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename


--
-- 2024.1 and beyond
--

set yb_enable_cbo = legacy_mode;

-- BNL enabled by default since 2024.1
set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename


--------------
-- Analyzed --
--------------

analyze t1, t2, t2r;

--
-- Pre-2024.1 behavior (20.2.x and earlier)
--

-- Pre-2024.1 defaults
set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;

set yb_enable_cbo = legacy_bnl_mode;
\i :filename

set yb_enable_cbo = legacy_ignore_stats_bnl_mode;
\i :filename

set yb_enable_cbo = legacy_stats_bnl_mode;
\i :filename

-- BNL enabled
set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;

set yb_enable_cbo = legacy_bnl_mode;
\i :filename

set yb_enable_cbo = legacy_ignore_stats_bnl_mode;
\i :filename

set yb_enable_cbo = legacy_stats_bnl_mode;
\i :filename


--
-- 2024.1 and beyond
--

-- BNL enabled by default since 2024.1
set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;

set yb_enable_cbo = legacy_mode;
\i :filename

set yb_enable_cbo = legacy_stats_mode;
\i :filename

-- BNL disabled
set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;

set yb_enable_cbo = legacy_mode;
\i :filename

set yb_enable_cbo = legacy_stats_mode;
\i :filename


drop table t1, t2, t2r;
