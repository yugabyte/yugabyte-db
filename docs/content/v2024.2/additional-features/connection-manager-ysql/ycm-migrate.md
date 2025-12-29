---
title: Migrate to YSQL Connection Manager
headerTitle: Migrate
linkTitle: Migrate
description: Migrate to YSQL Connection Manager
headcontent: How to migrate from your current pooling solution
menu:
  v2024.2:
    identifier: ycm-migrate
    parent: connection-manager
    weight: 50
type: docs
---

## PgBouncer

PgBouncer is a generic PostgreSQL connection pooler and Connection Manager is purpose-built for YugabyteDB, optimizing connection management for distributed PostgreSQL (YSQL). Both support transaction pooling, session pooling, or statement pooling.

The following table describes key differences between PgBouncer and YugabyteDB Connection Manager.

| Feature | PgBouncer | YSQL Connection Manager |
| :--- | :--- | :--- |
| Architecture | Single node | Designed for distributed multi-node connections |
| Pooling mode | Transaction level (default) | Transaction level only |
| Pool configuration | Creates pool for every combination of users and databases | Creates pool for every (user,db) combination |
| SQL limitations | No support for SQL features such as TEMP TABLE, SET statements, CURSORS, and so on. | No equivalent limitations. |
| Configuration parameters | max_db_connections | ysql_max_connections (core database flag) |
| | max_db_client_connections | ysql_conn_mgr_max_client_connections |
| | min_pool_size | ysql_conn_mgr_min_conns_per_db |
| | server_idle_timeout | ysql_conn_mgr_idle_time |
| | server_lifetime | ysql_conn_mgr_server_lifetime |
| | tcp_keepalive | ysql_conn_mgr_tcp_keepalive |
| | tcp_keepintvl | ysql_conn_mgr_tcp_keepalive_keep_interval |
| | listen_port | ysql_conn_mgr_port |
| Connection string | `postgresql://<username>:<password>@<pgbouncer_host>:<pgbouncer_port>/<database_name>?sslmode=require` | `postgresql://<username>:<password>@<host>:<port>/<database_name>?sslmode=require`<br><br>Connection Manager remains transparent, connection string (by default) is same as without connection manager enabled. |
| Scalability | Single process/thread. To scale, you need to start multiple instances of PgBouncer. | The number of threads for multiplexing is configurable using `ysql_conn_mgr_worker_threads` (default is CPU cores divided by 2). |

### Migrate

After reviewing the differences between PgBouncer and Connection Manager, migrate from PgBouncer as follows:

1. Make sure your application works with session-level pooling.

    Connection Manager uses session-level pooling. If you are using transaction pooling with PgBouncer, you may need to make changes to your application.

1. Deploy YugabyteDB Connection Manager.

    Enable and configure YugabyteDB Connection Manager. Refer to [setup](../ycm-setup/).

    [How to configure to connect to a local region (without using sticky session).]

1. Update connection strings.

    Change your application's database connection URL to point to the YugabyteDB endpoint (default port is 5433).

1. Test in a staging environment.

    - Validate connection handling when running your workload.
    - Simulate failover scenarios and node failures.

1. Set up [monitoring](../ycm-monitor/).

    - Integrate YugabyteDB metrics into Prometheus, Grafana, Datadog, or your preferred APM.
    - Track active connections, error rates, and latency.

1. Understand how YugabyteDB scales horizontally.

    PgBouncer is a single process/thread, and to scale you need to start multiple instances of PgBouncer. Connection manager on the other hand is multi-threaded and creates a number of threads based on the available CPU.

1. Update your failover and high availability strategy.

    Make sure your application handles dynamic leader changes. Remove PgBouncer-specific manual failover scripts, if any custom scripts are used to fail over and bring your application online.

<!-- ## HikariPool -->
