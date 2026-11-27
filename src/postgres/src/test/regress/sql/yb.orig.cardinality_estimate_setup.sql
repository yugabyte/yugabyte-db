--
-- A collection of queries to build the database objects used by the
-- cardinality estimation tests.
--

-- To avoid IF NOT EXISTS NOTICE messages.
SET client_min_messages TO WARNING;

-- the following plpgsql functions have been borrowed from vanilla pg tests

-- from gin.sql, removed guc settings and ANALYZE option
drop function if exists explain_query_json cascade;
create function explain_query_json(query_sql text)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute 'EXPLAIN (FORMAT json) ' || query_sql;
end;
$$;

-- from stats_ext.sql, removed the analyze option.
-- check the number of estimated/actual rows in the top node
drop function if exists check_estimated_rows cascade;
create function check_estimated_rows(text) returns table (estimated int, actual int)
language plpgsql as
$$
declare
    ln text;
    tmp text[];
    first_row bool := true;
begin
    for ln in
        execute format('explain analyze %s', $1)
    loop
        if first_row then
            first_row := false;
            tmp := regexp_match(ln, 'rows=(\d*) .* rows=(\d*)');
            return query select tmp[1]::int, tmp[2]::int;
        end if;
    end loop;
end;
$$;

-- modified the above to return estimates only
drop function if exists get_estimated_rows cascade;
create function get_estimated_rows(text) returns table (estimated int)
language plpgsql as
$$
declare
    ln text;
    tmp text[];
    first_row bool := true;
begin
    for ln in
        execute format('explain %s', $1)
    loop
        if first_row then
            first_row := false;
            tmp := regexp_match(ln, 'rows=(\d*)');
            return query select tmp[1]::int;
        end if;
    end loop;
end;
$$;


-- create and populate the test tables with uniformly distributed values
-- to make the estimates predictable.
drop table if exists r, s;
create table r (pk int, a int, b int, c char(10), d int, e int, bl bool, v char(666), primary key (pk asc));
create table s (x int, y int, z char(10));
create index i_r_a on r (a asc);
create index i_r_a_ge_1k on r (a asc) where a >= 1000;
create unique index i_r_b on r (b asc);
create index i_r_c on r (c asc);
create index i_r_bl on r (bl hash);

insert into r
  select
    i, ((i - 1) / 10) + 1, i,
    -- 'aaa', 'aab', 'aac', ...
    concat(chr((((i-1)/26/26) % 26) + ascii('a')),
           chr((((i-1)/26) % 26) + ascii('a')),
           chr(((i-1) % 26) + ascii('a'))),
    i, ((i - 1) / 10) + 1,
    i % 2 = 1,
    sha512(('x'||i)::bytea)::bpchar||lpad(sha512((i||'y')::bytea)::bpchar, 536, '#')
  from generate_series(1, 12345) i;

insert into s
  select
    ((i - 1) / 3) + 1, i,
    concat(chr((((i-1)/26/26) % 26) + ascii('a')),
           chr((((i-1)/26) % 26) + ascii('a')),
           chr(((i-1) % 26) + ascii('a')))
  from generate_series(1, 123) i;

alter table r alter column a set statistics 10000;
alter table r alter column b set statistics 10000;
alter table r alter column e set statistics 10000;

-- dependent columns: s = k % 5 and t = k % 5, so a value of k implies its s
-- and its t.  With the dependency statistics, "k = .. and s = .." is estimated
-- as the condition on k alone.  i_rd_k includes s and not t.
drop table if exists rd;
create table rd (pk int, k int, s int, t int, primary key (pk asc));
create index i_rd_k on rd (k asc) include (s);
create index i_rd_k_part on rd (k asc) where pk <= 6000;

insert into rd
  select i, i % 10, i % 5, i % 5
  from generate_series(1, 12345) i;

create statistics rd_k_s (dependencies) on k, s from rd;
create statistics rd_k_t (dependencies) on k, t from rd;
