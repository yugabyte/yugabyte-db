create extension yb_ycql_utils;
select ycql_stat_statements();
select * from ycql_stat_statements;

-- 1.1 <-> 1.2 upgrade and downgrade scripts.
drop extension yb_ycql_utils;
create extension yb_ycql_utils version '1.1';
select extversion from pg_extension where extname = 'yb_ycql_utils';
select prosrc = 'ycql_stat_statements' as uses_v1_1_symbol
from pg_proc where proname = 'ycql_stat_statements';
select exists (
    select 1 from pg_attribute
    where attrelid = 'ycql_stat_statements'::regclass
      and attname = 'yb_latency_histogram'
      and not attisdropped
) as has_yb_latency_histogram;

alter extension yb_ycql_utils update to '1.2';
select extversion from pg_extension where extname = 'yb_ycql_utils';
select prosrc = 'ycql_stat_statements_1_2' as uses_v1_2_symbol
from pg_proc where proname = 'ycql_stat_statements';
select exists (
    select 1 from pg_attribute
    where attrelid = 'ycql_stat_statements'::regclass
      and attname = 'yb_latency_histogram'
      and not attisdropped
) as has_yb_latency_histogram;

alter extension yb_ycql_utils update to '1.1';
select extversion from pg_extension where extname = 'yb_ycql_utils';
select prosrc = 'ycql_stat_statements' as uses_v1_1_symbol
from pg_proc where proname = 'ycql_stat_statements';
select exists (
    select 1 from pg_attribute
    where attrelid = 'ycql_stat_statements'::regclass
      and attname = 'yb_latency_histogram'
      and not attisdropped
) as has_yb_latency_histogram;
