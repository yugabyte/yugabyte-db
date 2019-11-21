pg_stat_monitor - Statists collector for [PostgreSQL][1].

The pg_stat_monitor is statistics collector tool based on PostgreSQL's pg_stat_statement.
#### Supported PostgreSQL Versions.
    *   PostgreSQL Version 11
    *   PostgreSQL Version 12
    *   Percona Dsitribution for PostgreSQL
#### Installation
There are two ways to install the pg_stat_monitor. The first is by downloading the pg_stat_monitor source code and compiling it. The second option is to download the deb or rpm packages.
##### Download and compile
The latest release of pg_stat_monitor can be downloaded from this GitHub page:

    https://github.com/Percona-Lab/pg_stat_monitor/releases
    
or it can be downloaded using the git:

    git clone git://github.com/Percona-Lab/pg_stat_monitor.git
    
After downloading the code, set the path for the [PostgreSQL][1] binary:

    cd pg_stat_monitor
    make USE_PGXS=1
    make USE_PGXS=1 install

#### Usage
There are four views, and complete statistics can be accessed using these views.

    \d pg_stat_monitor;
                          View "public.pg_stat_monitor"
           Column        |       Type       | Collation | Nullable | Default 
    ---------------------+------------------+-----------+----------+---------
    userid              | oid              |           |          | 
    dbid                | oid              |           |          | 
    queryid             | bigint           |           |          | 
    query               | text             |           |          | 
    calls               | bigint           |           |          | 
    total_time          | double precision |           |          | 
    min_time            | double precision |           |          | 
    max_time            | double precision |           |          | 
    mean_time           | double precision |           |          | 
    stddev_time         | double precision |           |          | 
    rows                | bigint           |           |          | 
    shared_blks_hit     | bigint           |           |          | 
    shared_blks_read    | bigint           |           |          | 
    shared_blks_dirtied | bigint           |           |          | 
    shared_blks_written | bigint           |           |          | 
    local_blks_hit      | bigint           |           |          | 
    local_blks_read     | bigint           |           |          | 
    local_blks_dirtied  | bigint           |           |          | 
    local_blks_written  | bigint           |           |          | 
    temp_blks_read      | bigint           |           |          | 
    temp_blks_written   | bigint           |           |          | 
    blk_read_time       | double precision |           |          | 
    blk_write_time      | double precision |           |          | 
    host                | integer          |           |          | 
    hist_calls          | text             |           |          | 
    hist_min_time       | text             |           |          | 
    hist_max_time       | text             |           |          | 
    hist_mean_time      | text             |           |          | 
    slow_query          | text             |           |          | 
    cpu_user_time       | double precision |           |          | 
    cpu_sys_time        | double precision |           |          | 
    
    regression=# \d pg_stat_agg_database 
                         View "public.pg_stat_agg_database"
         Column     |           Type           | Collation | Nullable | Default 
    ----------------+--------------------------+-----------+----------+---------
     queryid        | bigint                   |           |          | 
     dbid           | bigint                   |           |          | 
     userid         | oid                      |           |          | 
     host           | inet                     |           |          | 
     total_calls    | integer                  |           |          | 
     min_time       | double precision         |           |          | 
     max_time       | double precision         |           |          | 
     mean_time      | double precision         |           |          | 
     hist_calls     | text[]                   |           |          | 
     hist_min_time  | text[]                   |           |          | 
     hist_max_time  | text[]                   |           |          | 
     hist_mean_time | text[]                   |           |          | 
     first_log_time | timestamp with time zone |           |          | 
     last_log_time  | timestamp with time zone |           |          | 
     cpu_user_time  | double precision         |           |          | 
     cpu_sys_time   | double precision         |           |          | 
     query          | text                     |           |          | 
     slow_query     | text                     |           |          | 
    
    # \d pg_stat_agg_user
                           View "public.pg_stat_agg_user"
         Column     |           Type           | Collation | Nullable | Default 
    ----------------+--------------------------+-----------+----------+---------
     queryid        | bigint                   |           |          | 
     dbid           | bigint                   |           |          | 
     userid         | oid                      |           |          | 
     host           | inet                     |           |          | 
     total_calls    | integer                  |           |          | 
     min_time       | double precision         |           |          | 
     max_time       | double precision         |           |          | 
     mean_time      | double precision         |           |          | 
     hist_calls     | text[]                   |           |          | 
     hist_min_time  | text[]                   |           |          | 
     hist_max_time  | text[]                   |           |          | 
     hist_mean_time | text[]                   |           |          | 
     first_log_time | timestamp with time zone |           |          | 
     last_log_time  | timestamp with time zone |           |          | 
     cpu_user_time  | double precision         |           |          | 
     cpu_sys_time   | double precision         |           |          | 
     query          | text                     |           |          | 
     slow_query     | text                     |           |          | 

    # \d pg_stat_agg_host
                           View "public.pg_stat_agg_host"
         Column     |           Type           | Collation | Nullable | Default 
    ----------------+--------------------------+-----------+----------+---------
     queryid        | bigint                   |           |          | 
     dbid           | bigint                   |           |          | 
     userid         | oid                      |           |          | 
     host           | inet                     |           |          | 
     total_calls    | integer                  |           |          | 
     min_time       | double precision         |           |          | 
     max_time       | double precision         |           |          | 
     mean_time      | double precision         |           |          | 
     hist_calls     | text[]                   |           |          | 
     hist_min_time  | text[]                   |           |          | 
     hist_max_time  | text[]                   |           |          | 
     hist_mean_time | text[]                   |           |          | 
     first_log_time | timestamp with time zone |           |          | 
     last_log_time  | timestamp with time zone |           |          | 
     cpu_user_time  | double precision         |           |          | 
     cpu_sys_time   | double precision         |           |          | 
     query          | text                     |           |          | 
     slow_query     | text                     |           |          |    

#### Limitation
There are some limitations and Todo's. 

#### Licence
Copyright (c) 2006 - 2019, Percona LLC.
See [`LICENSE`][2] for full detail

[1]: https://www.postgresql.org/
[2]: LICENCE
[3]: https://github.com/Percona-Lab/pg_stat_monitor/issues/new
[4]: CONTRIBUTING.md
