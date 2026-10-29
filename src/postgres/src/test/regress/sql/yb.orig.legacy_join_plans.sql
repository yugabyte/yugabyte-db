--
-- Legacy mode Join Plans
--

set client_min_messages = 'warning';

drop table if exists t1, t2, t2r;

create table t1 (id int, a int);
create unique index nonconcurrently i_t1_id on t1 (id asc);

create table t2 (k1 int, k2 int, k3 int, k4 int, k5 int, v boolean, primary key (k1 hash));
create unique index nonconcurrently i_t2_k2 on t2 (k2 hash);
create index nonconcurrently i_t2_k3 on t2 (k3 hash);
create unique index nonconcurrently i_t2_k4k5 on t2 (k4 hash, k5 asc);
create index nonconcurrently i_t2_v on t2 (v hash);

create table t2r (k1 int, k2 int, k3 int, k4 int, k5 int, v boolean, primary key (k1 asc));
create unique index nonconcurrently i_t2r_k2 on t2r (k2 asc);
create index nonconcurrently i_t2r_k3 on t2r (k3 asc);
create unique index nonconcurrently i_t2r_k4k5 on t2r (k4 asc, k5 asc);
create index nonconcurrently i_t2r_v on t2r (v asc);

-- Load the tables non-transactionally.  A transactional load first writes
-- every row of t2 and t2r and of their indexes as a provisional record, and
-- the tservers apply and delete those records after the commit, while the
-- next statements run.  On a busy host that work can delay those statements
-- past their RPC timeouts (#36291).
set yb_disable_transactional_writes = on;
insert into t1 select i, i / 4 from generate_series(1, 1000) i;
insert into t2 select i, i, i % 10000, i % 10000, (i / 10000) * 10000, (i%2) = 1 from generate_series(1, 50000) i;
insert into t2r select i, i, i % 10000, i % 10000, (i / 10000) * 10000, (i%2) = 1 from generate_series(1, 50000) i;
set yb_disable_transactional_writes = off;


\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/legacy_join_plan_queries.sql'
\set costsfilename :abs_srcdir '/yb_commands/legacy_join_plan_queries_with_costs.sql'

set yb_ignore_bool_cond_for_legacy_estimate = on;

-- Set GUC parameters to restore PG11 behavior
set enable_memoize = false;
set hash_mem_multiplier = 1.0;

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

analyze t1;
analyze t2;
analyze t2r;

--
-- Pre-2024.1 behavior (20.2.x and earlier)
--

set yb_enable_cbo = legacy_ignore_stats_bnl_mode;

-- Pre-2024.1 defaults
set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;
\i :filename

set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename


set yb_enable_cbo = legacy_stats_bnl_mode;

-- Pre-2024.1 defaults
set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;
\i :filename

set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename


--
-- 2024.1 and beyond
--

set yb_enable_cbo = off;

set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename

set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;
\i :filename


set yb_enable_cbo = legacy_stats_mode;

set yb_bnl_batch_size = 1;
set yb_prefer_bnl = off;
\i :filename

set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :filename


--
-- not only the plans but the cost/row count estimates should never change
--

-- Pre-2024.1, BNL enabled
set yb_enable_cbo = legacy_ignore_stats_bnl_mode;
set yb_bnl_batch_size = 1024;
set yb_prefer_bnl = on;
\i :costsfilename


-- 2024.1 and beyond
set yb_enable_cbo = off;
\i :costsfilename


drop table t1, t2, t2r;
