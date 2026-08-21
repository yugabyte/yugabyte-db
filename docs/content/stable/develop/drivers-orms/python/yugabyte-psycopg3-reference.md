---
title: Yugabyte Psycopg 3 Smart Driver
headerTitle: Python drivers
linkTitle: Python drivers
description: Yugabyte Psycopg 3 Smart Driver for YSQL
tags:
  other: ysql
menu:
  stable_develop:
    name: Python drivers
    identifier: ref-yugabyte-psycopg3-driver
    parent: python-drivers
    weight: 105
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../yugabyte-psycopg2-reference/" class="nav-link">
      <img src="/icons/yugabyte.svg">
      YB Psycopg 2
    </a>
  </li>
  <li >
    <a href="../yugabyte-psycopg3-reference/" class="nav-link active">
      <img src="/icons/yugabyte.svg">
      YB Psycopg 3
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg2-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg 2
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg3-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg 3
    </a>
  </li>
</ul>

Yugabyte Psycopg 3 smart driver is a Python driver for [YSQL](../../../../api/ysql/) built on the [PostgreSQL Psycopg 3 driver](https://github.com/psycopg/psycopg), with additional connection load balancing features.

For more information on the Yugabyte Psycopg 3 smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [CRUD operations](../yugabyte-psycopg3/)
- [GitHub repository](https://github.com/yugabyte/psycopg)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Download the driver dependency

The Yugabyte Psycopg 3 smart driver requires Python 3.10 or later and system [libpq](https://www.postgresql.org/docs/current/libpq.html) installed (the same runtime requirement as upstream psycopg 3's pure-Python distribution). For installation details, see the [upstream psycopg 3 documentation](https://www.psycopg.org/psycopg3/docs/basic/install.html).

The fork is published on [PyPI](https://pypi.org/project/psycopg-yugabytedb/). Install it like any other Python package using pip:

```sh
$ pip install psycopg-yugabytedb
```

Or pin the version explicitly:

```sh
$ pip install "psycopg-yugabytedb==3.3.4.1"
```

To use the upstream connection pool with the smart driver, install the `[pool]` extra:

```sh
$ pip install "psycopg-yugabytedb[pool]"
```

## Fundamentals

Learn how to perform common tasks required for Python application development using the YugabyteDB psycopg 3 smart driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `load_balance_hosts` - enable cluster-aware load balancing by setting this property to `true`; disabled by default (omit the parameter or set to `false`). The libpq values `disable` and `random` pass through unchanged.
- `topology_keys` - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone` (use `*` for any zone in that cloud/region). List multiple placements to allow more than one zone; a tserver matches if it satisfies _any_ listed key (OR logic). Ignored if `load_balance_hosts` is `false`. Cloud and region wildcards are rejected at parse time.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `yb_servers_refresh_interval` parameter (clamped to [0, 600] seconds). After a failed connect to a TServer, the driver quarantines that host for `failed_host_reconnect_delay_secs` seconds (default 5, clamped to [0, 60]) before reconsidering it.

The driver uses one contact host from the `host` parameter to bootstrap (one is enough, the rest of the cluster is discovered via `yb_servers()`), and then keeps a long-lived control connection per cluster for periodic refresh.

For more information, see [Cluster-aware load balancing](../../smart-drivers/#cluster-aware-load-balancing).

### Use the driver

To use the driver, pass new connection properties for load balancing in the connection string or as keyword arguments.

To enable cluster-aware load balancing across all servers, you set the `load_balance_hosts` property to `true` in the connection string or keyword arguments, as per the following examples:

- Connection String

    ```python
    import psycopg

    conn = psycopg.connect(
    "host=h1 port=5433 user=yugabyte dbname=yugabyte "
    "load_balance_hosts=true"
    )
    ```

- Keyword arguments

    ```python
    conn = psycopg.connect(host="h1", port=5433, user="yugabyte", dbname="yugabyte", load_balance_hosts="true")
    ```

You can specify [multiple hosts](../yugabyte-psycopg3/#use-multiple-addresses) in the connection string in case the primary address fails. After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

To specify topology keys, you set the `topology_keys` property to comma-separated values in the connection string or keyword arguments, as per the following examples:

- Connection String

    ```python
    conn = psycopg.connect("dbname=database_name host=hostname port=5433 user=username password=password load_balance_hosts=true topology_keys=cloud.region.zone1")
    ```

- Keyword arguments

    ```python
    conn = psycopg.connect(user="username", password="password", host="hostname", port=5433, dbname="database_name", load_balance_hosts="true", topology_keys="cloud.region.zone1")
    ```

To allow load balancing across more than one placement, list several keys separated by commas. The driver treats the list as **OR** (union), which means a TServer is eligible if its placement matches _any_ key (and not every key). For example, `topology_keys=cloud1.datacenter1.zoneA,cloud2.datacenter2.zoneB` includes nodes in zoneA _or_ zoneB and excludes nodes in other zones.

- Connection String

    ```python
    conn = psycopg.connect("dbname=database_name host=hostname port=5433 user=username password=password load_balance_hosts=true topology_keys=cloud.region.zone1,cloud.region.zone2")
    ```

- Connection Dictionary

    ```python
    conn = psycopg.connect(user='username', password='password', host='hostname', port=5433, dbname='database_name', load_balance_hosts='true', topology_keys='cloud.region.zone1,cloud.region.zone2')
    ```

The zone segment may be `*` to match any zone within that cloud/region (for example, `topology_keys=cloud1.datacenter1.*`). Cloud and region wildcards are not allowed.

To configure a connection pool, install the `[pool]` extra and specify load balancing in the pool connection string as follows:

```python
from psycopg_pool import ConnectionPool

pool = ConnectionPool(
    "host=h1,h2,h3 port=5433 user=yugabyte dbname=yugabyte "
    "load_balance_hosts=true topology_keys=cloud1.datacenter1.zoneA",
    min_size=4,
    max_size=20,
)

with pool.connection() as conn:
    with conn.cursor() as cur:
        cur.execute("SELECT 1")
        cur.fetchone()

```

An equivalent async path exists via `AsyncConnection` and `AsyncConnectionPool`; the smart driver dispatcher is the same.

## Try it out

This tutorial shows how to use the Yugabyte Psycopg 3 driver with YugabyteDB. It starts by creating a 3 node cluster with a replication factor of 3.

Next, you use a Python shell terminal to demonstrate the driver's load balancing features by running a few Python scripts.

{{< note title="Note">}}
On macOS, add loopback aliases for `127.0.0.2` and `127.0.0.3` once per boot before starting nodes 2 and 3 on the same machine: `sudo ifconfig lo0 alias 127.0.0.2/32 up` (and similarly for `127.0.0.3`).
{{< /note >}}

### Create a local cluster

Create a universe with a 3-node RF-3 cluster with some fictitious geo-locations assigned. The placement values used are just tokens and have nothing to do with actual cloud regions and zones. Nodes 1 and 2 are in `cloud1.datacenter1.zoneA`; node 3 is in `cloud1.datacenter1.zoneB`.

```sh
cd <path-to-yugabytedb-installation>
```

Do the following:

1. Start the first node by running the yugabyted start command, passing in the `--cloud_location` and `--fault_tolerance` flags to set the node location details, as follows:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1 \
        --cloud_location=cloud1.datacenter1.zoneA \
        --fault_tolerance=zone
    ```

1. Start the second and the third node using the `--join` flag.

    For the second node, use the IP address of the first node in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.2 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2 \
        --cloud_location=cloud1.datacenter1.zoneA \
        --fault_tolerance=zone
    ```

    For the third node, you can use the IP address of any currently running node in the universe (for example, the first or the second node) in the `--join` flag:

    ```sh
    ./bin/yugabyted start --advertise_address=127.0.0.3 \
        --join=127.0.0.1 \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3 \
        --cloud_location=cloud1.datacenter1.zoneB \
        --fault_tolerance=zone
    ```

The cluster listens on loopback addresses `127.0.0.1`, `127.0.0.2`, and `127.0.0.3` on port 5433.

### Check uniform load balancing

Log into your Python terminal and run the following script:

```python
import psycopg
from psycopg.yb.registry import ClusterRegistry

DSN = (
    "host=127.0.0.1,127.0.0.2,127.0.0.3 port=5433 "
    "user=yugabyte dbname=yugabyte load_balance_hosts=true"
)

conns = [psycopg.connect(DSN) for _ in range(12)]
try:
    uuid = conns[0]._yb_uuid
    registry = ClusterRegistry.instance()
    for host in ("127.0.0.1", "127.0.0.2", "127.0.0.3"):
        print(f"{host}: {registry.get_load(uuid, host)} connections")
finally:
    for c in conns:
        c.close()
```

The application creates 12 connections. You should see four connections on each node:

```text
127.0.0.1: 4 connections
127.0.0.2: 4 connections
127.0.0.3: 4 connections
```

To verify the behavior from the server side, wait for the app to create connections and then visit `http://<host>:13000/rpcz` from your browser for each node, or run:

```sh
for h in 127.0.0.1 127.0.0.2 127.0.0.3; do
    echo -n "$h: "; curl -s http://$h:13000/rpcz | grep -c "client backend"
done
```

Expected `/rpcz` counts are 5, 4, and 4 — four client connections per host, plus one extra on whichever host holds the smart driver's long-lived control connection.

### Check topology-aware load balancing

Run the following script in your Python terminal with the `topology_keys` property set to `cloud1.datacenter1.zoneA`; only the two zoneA nodes receive connections.

```python
import psycopg
from psycopg.yb.registry import ClusterRegistry

DSN = (
    "host=127.0.0.1,127.0.0.2,127.0.0.3 port=5433 "
    "user=yugabyte dbname=yugabyte "
    "load_balance_hosts=true topology_keys=cloud1.datacenter1.zoneA"
)

conns = [psycopg.connect(DSN) for _ in range(12)]
try:
    uuid = conns[0]._yb_uuid
    registry = ClusterRegistry.instance()
    for host in ("127.0.0.1", "127.0.0.2", "127.0.0.3"):
        print(f"{host}: {registry.get_load(uuid, host)} connections")
finally:
    for c in conns:
        c.close()
```

You should see six connections on each of the first two nodes and zero on the third:

```text
127.0.0.1: 6 connections
127.0.0.2: 6 connections
127.0.0.3: 0 connections
```

To verify the behavior, wait for the app to create connections and then navigate to `http://<host>:13000/rpcz`. The zoneB node should have zero client backend entries from this workload.

### Clean up

When you're done experimenting, run the following commands to destroy the local cluster:

```sh
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node1
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node2
./bin/yugabyted destroy --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node3
```

## Limitations

- Currently, [PostgreSQL Psycopg 3 driver](https://github.com/psycopg/psycopg) and [YugabyteDB Psycopg 3 smart driver](https://github.com/yugabyte/psycopg) _cannot_ be used in the same environment. Both write to `site-packages/psycopg/`. If a previous environment has upstream psycopg (or psycopg-binary, psycopg-c) installed, uninstall it before installing the fork, or use a fresh virtual environment.

- Pure Python distribution only. The C-accelerated (psycopg-yugabytedb-c) and pre-built binary (psycopg-yugabytedb-binary) distributions are not published yet. The pure-Python distribution requires system libpq.

- Strict topology filter in this release. If `topology_keys` matches no live nodes, connect raises `OperationalError` (no cluster-wide fallback in this release).
