---
title: YugabyteDB Psycopg 3 Smart Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Python application using YugabyteDB Psycopg 3 Smart Driver for YSQL
menu:
  stable_develop:
    identifier: yugabyte-psycopg3-driver
    parent: python-drivers
    weight: 505
rightNav:
  hideH4: true
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yugabyte-psycopg3/" class="nav-link">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../yugabyte-psycopg2" class="nav-link">
      <img src="/icons/yugabyte.svg" /i>
      Yugabyte Psycopg2
    </a>
  </li>
  <li >
    <a href="../yugabyte-psycopg3" class="nav-link active">
      <img src="/icons/yugabyte.svg" /i>
      Yugabyte Psycopg 3
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg2" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg2
    </a>
  </li>
  <li >
    <a href="../postgres-psycopg3" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG Psycopg3
    </a>
  </li>
  <li >
    <a href="../aiopg" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      aiopg
    </a>
  </li>
</ul>

The [Yugabyte Psycopg 3 smart driver](https://pypi.org/project/psycopg-yugabytedb/) is a Python driver for [YSQL](/stable/api/ysql/) built on the [PostgreSQL Psycopg 3 driver](https://github.com/psycopg/psycopg2), with additional [connection load balancing](/stable/develop/drivers-orms/smart-drivers/) features. The import name is `psycopg`, so no changes are needed on existing application code; opting in requires a single parameter on the connection string.

This guide uses YSQL with the YugabyteDB Psycopg 3 smart driver.

## CRUD operations

The following sections demonstrate how to perform common tasks required for Python application development using the YugabyteDB Psycopg 3 smart driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Add the YugabyteDB driver dependency

The fork is published as a pre-release on PyPI. Install with `--pre` or pin the exact version:

```sh
pip install --pre psycopg-yugabytedb
# or
pip install "psycopg-yugabytedb==3.3.4.1"
```

To use the upstream connection pool alongside the smart driver, install the `[pool]` extra. This pulls in an unmodified upstream `psycopg-pool`; the pool's connections still go through the smart-driver dispatcher.

```sh
pip install --pre "psycopg-yugabytedb[pool]"
```

#### Prerequisites

- Python 3.10 or later
- `libpq` installed on the system (the C library underneath every PostgreSQL client). This is the same runtime prerequisite as upstream psycopg 3's pure-Python distribution.
  - macOS: `brew install libpq && brew link --force libpq`
  - Ubuntu / Debian: `sudo apt install libpq5`
  - RHEL / Fedora: `sudo dnf install libpq`

{{< note title="Coexistence with upstream psycopg" >}}

When you install `psycopg-yugabytedb` from PyPI, your code still uses `import psycopg` — the same module name as [upstream Psycopg 3](https://www.psycopg.org/psycopg3/). Both packages install into the same directory (`site-packages/psycopg/`), so pip cannot install them side by side. If either package is already present, uninstall it first, or use a dedicated virtual environment:

| PyPI package (install with pip) | Import in application code |
| :------------------------------ | :------------------------- |
| `psycopg-yugabytedb` | `import psycopg` (Yugabyte smart driver) |
| `psycopg`, `psycopg-binary`, or `psycopg-c` | `import psycopg` (upstream only) |

```sh
python3 -m venv ~/yb-venv
~/yb-venv/bin/pip install --pre psycopg-yugabytedb
```

{{< /note >}}

Verify the install:

```sh
python -c "import psycopg; print(psycopg.__version__)"
# Expected: 3.3.4.1

python -c "from psycopg.yb.registry import ClusterRegistry; print('smart driver ok')"
# Expected: smart driver ok
```

### Step 2: Set up the database connection

To enable smart-driver load balancing, add `load_balance_hosts=true` to your connection string (or pass `load_balance_hosts="true"` to `psycopg.connect()`). If you omit it, the driver behaves like upstream Psycopg 3.

The smart driver extends the libpq conninfo string with four parameters. All take the standard libpq `key=value` form, and both dashed (`load-balance-hosts`) and underscore forms are accepted.

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Comma-separated hostnames or IPs. One contact host is enough to bootstrap. The rest of the cluster is discovered via `yb_servers()`. Multiple hosts add robustness if the first contact is down. | localhost |
| port | Listen port for YSQL. | 5433 |
| dbname | Database name | yugabyte |
| user | User connecting to the database | yugabyte |
| password | User password | yugabyte |
| load_balance_hosts | Enables [uniform load balancing](../../smart-drivers/#cluster-aware-load-balancing). Set to `true` to use the smart driver. | false |
| topology_keys | Enables [topology-aware load balancing](../../smart-drivers/#topology-aware-load-balancing). Comma-separated placements as `cloud.region.zone` (use `*` for any zone in that cloud/region). Ignored if `load_balance_hosts` is `false`. | none |
| yb_servers_refresh_interval | How often the driver re-queries `yb_servers()` to pick up topology changes. Clamped to [0, 600]. | 300 |
| failed_host_reconnect_delay_secs | After a failed connect to a TServer, how long the driver quarantines that host before reconsidering it. Clamped to [0, 60]. | 5 |

The following is an example connection string:

```python
import psycopg

conn = psycopg.connect(
"host=h1,h2,h3 port=5433 user=yugabyte dbname=yugabyte "
"load_balance_hosts=true"
)
```

You can also pass the parameters as keyword arguments to `psycopg.connect`:

```python
conn = psycopg.connect(host="h1,h2,h3", port=5433, user="yugabyte", dbname="yugabyte", load_balance_hosts="true")
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails.

{{< tip title="Tip">}}
To obtain a list of available hosts, you can connect to any cluster node and use the YSQL `yb_servers()` function.
{{< /tip >}}

Delimit the addresses using commas, as follows:

```python
conn = psycopg.connect(
    "host=host1,host2,host3 port=5433 user=yugabyte dbname=yugabyte "
    "password=yugabyte load_balance_hosts=true"
)
```

The hosts are only used during the initial connection attempt. If the first host is down when the driver is connecting, libpq tries each host in turn and the smart driver bootstraps from the first one that responds. After bootstrap, the driver discovers the rest of the cluster via `yb_servers()`.

#### Use SSL

The following table describes the connection parameters required to connect using SSL.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| sslmode | SSL mode | prefer |
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/ |

The following is an example for connecting to a YugabyteDB cluster with SSL enabled:

```python
conn = psycopg.connect("host=<hostname> port=5433 dbname=yugabyte user=<username> password=<password> load_balance_hosts=true sslmode=verify-full sslrootcert=/path/to/root.crt")
```

SSL uses libpq's standard parameters — the same flags as upstream psycopg 3. For details, see the [PostgreSQL libpq SSL documentation](https://www.postgresql.org/docs/current/libpq-ssl.html).

### Step 3: Write your application

Create a new Python file called `QuickStartApp.py` in the base package directory of your project.

Copy the following sample code to set up tables and query the table contents. Replace the connection string `connString` with the cluster credentials and SSL certificate, if required.

```python
import psycopg

# Create the database connection.

connString = "host=127.0.0.1 port=5433 dbname=yugabyte user=yugabyte password=yugabyte load_balance_hosts=true"

conn = psycopg.connect(connString)

# Open a cursor to perform database operations.

conn.autocommit = True
cur = conn.cursor()

# Create the table. (It might preexist.)

cur.execute(
  """
  DROP TABLE IF EXISTS employee
  """)

cur.execute(
  """
  CREATE TABLE employee (id int PRIMARY KEY,
                        name varchar,
                        age int,
                        language varchar)
  """)
print("Created table employee")
cur.close()

# Take advantage of ordinary, transactional behavior for DMLs.

conn.autocommit = False
cur = conn.cursor()

# Insert a row.

cur.execute("INSERT INTO employee (id, name, age, language) VALUES (%s, %s, %s, %s)",
            (1, 'John', 35, 'Python'))
print("Inserted (id, name, age, language) = (1, 'John', 35, 'Python')")

# Query the row.

cur.execute("SELECT name, age, language FROM employee WHERE id = 1")
row = cur.fetchone()
print("Query returned: %s, %s, %s" % (row[0], row[1], row[2]))

# Commit and close down.

conn.commit()
cur.close()
conn.close()
```

## Run the application

Run the project `QuickStartApp.py` using the following command:

```python
python3 QuickStartApp.py
```

You should see output similar to the following:

```text
Created table employee
Inserted (id, name, age, language) = (1, 'John', 35, 'Python')
Query returned: John, 35, Python
```

If the connection fails, verify:

- YugabyteDB is running and reachable on the host/port in your DSN.
- The yugabyte user (or whichever user you used) exists and has the necessary permissions.
- For multi-host DSNs, at least one contact host responds.
- System libpq is installed (see [Prerequisites](#prerequisites)).

## Limitations

- Currently, [PostgreSQL psycopg 3 driver](https://github.com/psycopg/psycopg) and [YugabyteDB psycopg 3 smart driver](https://github.com/yugabyte/psycopg) _cannot_ be used in the same environment. Both write to `site-packages/psycopg/`. If a previous environment has upstream psycopg (or psycopg-binary, psycopg-c) installed, uninstall it before installing the fork, or use a fresh virtual environment.

- The driver is a pre-release (3.3.4.1rc1); install with `--pre` or pin the exact version.

- Pure Python distribution only. The C-accelerated (psycopg-yugabytedb-c) and pre-built binary (psycopg-yugabytedb-binary) distributions are not published yet. The pure-Python distribution requires system libpq.

- Strict topology filter in this release. If `topology_keys` matches no live nodes, connect raises `OperationalError` (no cluster-wide fallback in this release).

## Learn more

[YugabyteDB smart drivers for YSQL](../../smart-drivers/)
