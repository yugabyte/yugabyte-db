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

Yugabyte Psycopg 3 smart driver is a Python driver for [YSQL](../../../../api/ysql/) built on the [PostgreSQL Psycopg 3 driver](https://github.com/psycopg/psycopg), with additional connection load balancing and [xCluster failover](#xcluster-failover) features.

For more information on the Yugabyte Psycopg 3 smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [CRUD operations](../yugabyte-psycopg3/)
- [GitHub repository](https://github.com/yugabyte/psycopg)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Download the driver dependency

The Yugabyte Psycopg 3 smart driver requires Python 3.10 or later. The fork is published on [PyPI](https://pypi.org/project/psycopg-yugabytedb/). Install it like any other Python package using pip.

To install the driver with the pre-built binary C extension, which bundles [libpq](https://www.postgresql.org/docs/current/libpq.html), use the `[binary]` extra. Binary packages are available for Linux x86_64 and macOS on Apple Silicon (arm64):

```sh
$ pip install "psycopg-yugabytedb[binary]"
```

On other platforms, install the pure-Python distribution, which requires system libpq (the same runtime requirement as upstream psycopg 3's pure-Python distribution):

```sh
$ pip install psycopg-yugabytedb
```

To build the C extension from source against the system libpq, use the `[c]` extra, which requires a C compiler and the libpq headers. For installation details, see the [upstream psycopg 3 documentation](https://www.psycopg.org/psycopg3/docs/basic/install.html).

To pin the version explicitly:

```sh
$ pip install "psycopg-yugabytedb==3.3.4.2"
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

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `yb_servers_refresh_interval` parameter (clamped to [0, 600] seconds). When [xCluster failover](#xcluster-failover) is enabled, the same interval also sets how often the circuit breakers are checked (at most once per second). After a failed connect to a TServer, the driver quarantines that host for `failed_host_reconnect_delay_secs` seconds (default 5, clamped to [0, 60]) before reconsidering it. A host that refuses the connection is quarantined after the first failure; a host that doesn't answer within `connect_timeout` is quarantined after three consecutive timeouts, so that a briefly slow node stays in rotation.

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

## xCluster failover

xCluster failover lets an application keep running when a YugabyteDB universe becomes unavailable. It is for two universes that replicate to each other using [xCluster](../../../../deploy/multi-dc/async-replication/): a primary universe that the application normally uses, and a secondary universe.

- **Failover** - when the primary universe becomes unavailable, the driver sends new connections to the secondary universe.
- **Failback** - when the primary universe is available again, the driver moves connections back to it.

xCluster failover is available in Yugabyte Psycopg 3 smart driver v3.3.4.2 or later.

### How it works

The driver doesn't decide by itself whether a universe is available. The application provides a _circuit breaker_ for each universe: an object that reports whether that universe is healthy, based on the signal the application trusts (a probe query, a monitoring system, or an operator). The driver checks both circuit breakers periodically in the background.

The two universes, their circuit breakers, and the current routing state together make up a _failover group_. A process has one failover group for each primary universe, and every connection to that universe belongs to the group, so all of them switch universes together.

A switch from one universe to the other happens in two phases:

1. **Drain** - the driver stops opening new connections, and gives the transactions already running on the current universe time to finish. Connections that are still open when the time is up are closed, so that no more writes reach the universe being left.
1. **Replication lag wait** - if YugabyteDB Anywhere (YBA) is configured, the driver checks the xCluster replication lag, and waits until the other universe has received the writes made before the drain. Without YBA, this phase is skipped.

New connections then go to the other universe. If the replication lag wait runs out of time, a failover proceeds anyway, as the primary universe is unavailable. A failback is postponed and retried after a cooldown, so that the application isn't moved back to a primary universe that is still missing recent writes.

If both universes are unhealthy, new connections fail with `NoViableClusterError`.

### xCluster failover connection properties

The following connection properties enable xCluster failover and control its main behavior:

- `load_balance_hosts` - must be set to `true`; xCluster failover is part of the smart driver.
- `host` - the hosts of the primary universe. One reachable host is enough; the rest of the universe is discovered.
- `yb.failover.secondaryClusterHosts` - comma-separated hosts of the secondary universe. Setting this property turns on xCluster failover.
- `yb_servers_refresh_interval` - the node-list refresh interval described in [Load balancing connection properties](#load-balancing-connection-properties), which with xCluster failover also sets how often, in seconds, the circuit breakers are checked (default 300). Set it to a few seconds for timely failure detection.
- `yb.failover.drainTimeoutSecs` - how long running transactions get to finish during the drain (default 10). `-1` waits for all of them; `0` closes connections immediately.
- `yb.failover.lagWaitTimeoutSecs` - maximum duration of the replication lag wait (default 30).
- `yb.failover.globalTimeBudgetSecs` - an upper limit on the whole switch (unset by default). When set, it is split between the drain (40%) and the replication lag wait (60%), unless those are set explicitly.
- `yb.failover.cooldownSecs` - minimum time between two status changes of the same circuit breaker (default 1500). This prevents switching back and forth when a universe is unstable.
- `yb.failover.autoFailbackEnabled` - set to `false` to stay on the secondary universe after the primary universe recovers, until you fail back manually (default `true`).

To enable the replication lag wait, add the following properties:

- `yb.failover.ybaEndpoint` - the YugabyteDB Anywhere base URL, for example `https://yba.example.com`.
- `yb.failover.ybaApiToken` - a YugabyteDB Anywhere [API token](../../../../yugabyte-platform/anywhere-automation/anywhere-api/).
- `yb.failover.replicationName` - the name of the [xCluster replication configuration](../../../../yugabyte-platform/create-deployments/xcluster-replication/xcluster-replication-setup/) from the primary to the secondary universe, used on failover.
- `yb.failback.replicationName` - the name of the replication configuration from the secondary to the primary universe, used on failback.
- `yb.failover.thresholdReplicationLagMs` - the driver switches once the replication lag is at or below this value (default 0).

Each `yb.failover.*` timing property has a `yb.failback.*` counterpart for the failback direction; when it isn't set, the failover value is used. For the complete list of properties, see the driver's [README](https://github.com/yugabyte/psycopg#xcluster-failover).

### Use xCluster failover

The following example configures xCluster failover for a primary universe with the nodes `a1`, `a2`, and `a3`, and a secondary universe with the nodes `b1`, `b2`, and `b3`. The circuit breakers are checked every 5 seconds, and running transactions get up to 10 seconds to finish before a switch:

```python
DSN = (
    "host=a1,a2,a3 port=5433 user=yugabyte dbname=yugabyte "
    "load_balance_hosts=true "
    "yb.failover.secondaryClusterHosts=b1,b2,b3 "
    "yb_servers_refresh_interval=5 "
    "yb.failover.drainTimeoutSecs=10 "
    "options='-c statement_timeout=10000'"
)
```

To enable the replication lag wait, add the YugabyteDB Anywhere properties:

```python
DSN += (
    " yb.failover.ybaEndpoint=https://yba.example.com"
    " yb.failover.ybaApiToken=<yba-api-token>"
    " yb.failover.replicationName=primary-to-secondary"
    " yb.failback.replicationName=secondary-to-primary"
    " yb.failover.lagWaitTimeoutSecs=30"
)
```

Create the failover group with `bootstrap_failover_group()` once when the application starts, and attach a circuit breaker for each universe before opening the first connection. Connecting without circuit breakers raises `MissingCircuitBreakerError`. Subsequent `psycopg.connect()` calls with the same connection string use this group.

```python
import psycopg
from psycopg.yb import bootstrap_failover_group
from psycopg.yb.health import HealthResult

class MyCircuitBreaker:
    def __init__(self, which_cluster):
        self.which_cluster = which_cluster   # "primary" or "secondary"

    def check(self, group):
        # Ask your monitoring system whether this universe is usable.
        if universe_is_down(self.which_cluster):
            return HealthResult.UNHEALTHY
        return HealthResult.HEALTHY

group = bootstrap_failover_group(DSN)
group.primary_circuit_breaker = MyCircuitBreaker("primary")
group.secondary_circuit_breaker = MyCircuitBreaker("secondary")

conn = psycopg.connect(DSN)
```

Use `abootstrap_failover_group()` in asyncio applications. Use a separate circuit breaker instance for each universe.

The [GitHub repository](https://github.com/yugabyte/psycopg/tree/3.3.4.x/demo/samples) includes sample circuit breakers that you can copy into your application: `TrackerTableCircuitBreaker` (a probe query across the universe), `ExternalSignalCircuitBreaker` (an operator fails over and fails back by inserting a row into a signal table), and `HealthAndLagCircuitBreaker` (a probe query combined with a replication lag check).

To use a connection pool, create the failover group and attach the circuit breakers first, and then create the pool with the same connection string. Pass `xcluster_check` so that the pool drops connections to the old universe after a switch:

```python
from psycopg.yb.pool import xcluster_check   # xcluster_check_async for AsyncConnectionPool
from psycopg_pool import ConnectionPool

pool = ConnectionPool(DSN, check=xcluster_check, min_size=4, max_size=20)
```

{{< note title="Set statement_timeout" >}}
YugabyteDB doesn't cancel a running query when its client connection is closed. For the drain to stop work on the old universe, set `statement_timeout` to no more than the drain timeout. Note that `statement_timeout` is in milliseconds and `yb.failover.drainTimeoutSecs` is in seconds; for example, use `statement_timeout=10000` for `drainTimeoutSecs=10`. Set it in the connection string (as in the preceding example) or for the role. The driver logs a warning at startup if `statement_timeout` is unset or larger than the drain timeout.
{{< /note >}}

### Manual failback

When `yb.failover.autoFailbackEnabled` is `false`, the driver stays on the secondary universe after the primary universe recovers. To fail back, call `trigger_failback()` from your application, for example from an admin endpoint or a maintenance script, in the process that created the failover group:

```python
from psycopg.yb.registry import ClusterRegistry

registry = ClusterRegistry.instance()
registry.trigger_failback(group.primary.uuid)

# (primary status, secondary status, last change times)
print(registry.get_failover_status(group.primary.uuid))
```

The manual failback runs the same drain and replication lag wait as an automatic failback, and blocks until they finish. It also restarts the primary universe's cooldown, so call it only when connections are on the secondary universe and the primary universe is known to be healthy.

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

- Strict topology filter in this release. If `topology_keys` matches no live nodes, connect raises `OperationalError` (no cluster-wide fallback in this release).

- xCluster failover decisions are made per process. In a pre-fork server (for example, gunicorn with `--preload`), the process that creates the failover group makes the decisions and forked workers follow it; call `trigger_failback()` in that process, not in a worker.
