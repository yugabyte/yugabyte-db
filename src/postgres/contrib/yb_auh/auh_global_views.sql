-- This script establishes connection with all the servers in the YB cluster
-- and creates a global view for pg_stat_statements and pg_active_universe_history.
-- Since password is needed, we need to pass it as custom.password parameter as show below
-- Usage:
-- $ PGOPTIONS="-c custom.password=${yb_passwd}" bin/ysqlsh -U yugabyte -f auh_global_views.sql  

create extension if not exists postgres_fdw;

select format('
 create server if not exists "gv$%1$s"
 foreign data wrapper postgres_fdw
 options (host %2$L, port %3$L, dbname %4$L)
 ', host, host, port, current_database()) from yb_servers();
\gexec

select format('
 drop user mapping if exists for admin
 server "gv$%1$s"
 ',host) from yb_servers();
\gexec

select format('
 create user mapping if not exists for current_user
 server "gv$%1$s"
 --options ( user %2$L, password %3$L ) 
 ',host, 'admin',  current_setting('custom.password'))
 from yb_servers();
\gexec

select format('
 drop schema if exists "gv$%1$s" cascade
 ',host) from yb_servers();
\gexec

select format('
 create schema if not exists "gv$%1$s"
 ',host) from yb_servers();
\gexec

select format('
 import foreign schema "pg_catalog"
 limit to ("pg_stat_statements")
 from server "gv$%1$s" into "gv$%1$s"
 ', host) from yb_servers();
\gexec

select format('
 import foreign schema "public"
 limit to ("pg_active_universe_history")
 from server "gv$%1$s" into "gv$%1$s"
 ', host) from yb_servers();
\gexec

-- create global view
with views as (
select distinct foreign_table_name
from information_schema.foreign_tables t, yb_servers() s
where foreign_table_schema = format('gv$%1$s',s.host)
)
select format('drop view if exists "gv$%1$s"', foreign_table_name) from views
union all
select format('create or replace view "gv$%2$s" as %1$s',
 string_agg(
 format('
 select %2$L as gv$host, %3$L as gv$zone, %4$L as gv$region, %5$L as gv$cloud,
 * from "gv$%2$s".%1$I
 ', foreign_table_name, host, zone, region, cloud)
 ,' union all '), foreign_table_name
) from views, yb_servers() group by views.foreign_table_name ;
\gexec
