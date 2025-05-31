---
title: Migrate to YSQL Connection Manager
headerTitle: Migrate
linkTitle: Migrate
description: Migrate to YSQL Connection Manager
headcontent: How to migrate from your current pooling solution
menu:
  preview:
    identifier: ycm-migrate
    parent: connection-manager
    weight: 50
type: docs
---

## PgBouncer

PgBouncer is a generic PostgreSQL connection pooler and YB-Controller is purpose-built for YugabyteDB, optimizing connection management for distributed PostgreSQL (YSQL). Both support transaction pooling, session pooling, or statement pooling.
Below table shows mapping and differences between PgBouncer and Yugabyte.

| Feature | PgBouncer | YSQL Connection Manager |
| :--- | :--- | :--- |
| Architecture | Single node | Designed for distributed multi-node connections |
| Pooling mode | Transaction level (default) | Transaction level only |
| Pool configuration | Creates pool for every combination of users and databases | Creates pool for every (user,db) combination |
| SQL limitations | Doesn't support SQL features such as TEMP TABLE, SET statements, CURSORS etc | Supported |
| Config parameters | max_db_connections | ysql_max_connections (core db flag) |
| | max_db_client_connections | ysql_conn_mgr_max_client_connections |
| | min_pool_size | ysql_conn_mgr_min_conns_per_db |
| | server_idle_timeout | ysql_conn_mgr_idle_time |
| | server_lifetime | ysql_conn_mgr_server_lifetime |
| | tcp_keepalive | ysql_conn_mgr_tcp_keepalive |
| | tcp_keepintvl | ysql_conn_mgr_tcp_keepalive_keep_interval |
| | listen_port | ysql_conn_mgr_port |
| Connection string | `postgresql://<username>:<password>@<pgbouncer_host>:<pgbouncer_port>/<database_name>?sslmode=require` | `postgresql://<username>:<password>@<host>:<port>/<database_name>?sslmode=require`<br><br>Connection Manager remains transparent, connection string (by default) is same as without connection manager enabled. |
| Scalability | Single process/thread. To scale, you need to start multiple instances of PgBouncer. | The number of threads for multiplexing is configurable. (`ysql_conn_mgr_worker_threads`, default value = CPU cores divided by 2) |

To migrate:

1. Understand Differences

    Review how PgBouncer differs from YB-Controller (Yugabyte-native pooling and load balancing) as explained in the table above.

1. Choose Pooling Mode

    - Use session pooling in Yugabyte (default session level) compared to PgBouncer
    - Plan application adjustments if PgBouncer was using transaction pooling.

1. Deploy Yugabyte Connection Manager

    - Enable and configure Yugabyte Connection Pooling
    - How to configure to connect to a local region (without using sticky session)

1. Update Connection Strings

    Change your application's database connection URL to point to the YugabyteDB endpoint (default port 5433).

1. Test in Staging Environment

    - Validate connection handling your work load.
    - Simulate failover scenarios and node failures.

1. Set Up Monitoring
    - Integrate YBDB metrics into Prometheus, Grafana, Datadog, or preferred APM.
    - Track active connections, error rates, and latency.

1. Scale horizontally

    PgBouncer is a Single process/thread. To scale, you need to start multiple instances of PgBouncer. YSQL Connection manager is multi-threaded and creates a number of threads based on the available CPU.

1. Update failover and high availability strategy

    - Ensure the application handles dynamic leader changes.
    - Remove PgBouncer-specific manual failover scripts if any custom scripts used to failover/bring your app online.

## HikariPool
