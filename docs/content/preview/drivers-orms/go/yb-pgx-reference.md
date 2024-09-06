---
title: PGX Smart Driver
headerTitle: Go Drivers
linkTitle: Go Drivers
description: Go PGX Smart Driver for YSQL
badges: ysql
aliases:
  - /preview/reference/drivers/go/
  - /preview/reference/drivers/go/yb-pgx-reference/
menu:
  preview:
    name: Go Drivers
    identifier: ref-1-yb-pgx-go-driver
    parent: go-drivers
    weight: 100
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../yb-pgx-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
       YugabyteDB PGX Smart Driver
    </a>
  </li>

  <li >
    <a href="../pgx-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="../pq-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

YugabyteDB PGX smart driver is a Go driver for [YSQL](../../../api/ysql/) based on [PGX](https://github.com/jackc/pgx/), with additional connection load balancing features.

For more information on the YugabyteDB PGX smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [CRUD operations](../yb-pgx)
- [GitHub repository](https://github.com/yugabyte/pgx)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Import the driver package

You can import the YugabyteDB PGX driver package by adding the following import statement in your Go code.

```go
import (
  "github.com/yugabyte/pgx/v5"
)
```

Optionally, you can choose to import the pgxpool package instead. Refer to [Using pgxpool API](#using-pgxpool-api) to learn more.

## Fundamentals

Learn how to perform common tasks required for Go application development using the YugabyteDB PGX driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `load_balance` - enable cluster-aware load balancing by setting this property to `true`; disabled by default.
- `topology_keys` - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone`. Specify all zones in a region as `cloud.region.*`. To designate fallback locations for when the primary location is unreachable, specify a priority in the form `:n`, where `n` is the order of precedence. For example, `cloud1.datacenter1.rack1:1,cloud1.datacenter1.rack2:2`.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `yb_servers_refresh_interval` connection parameter.

### Use the driver

To use the driver, pass new connection properties for load balancing in the connection URL or properties pool.

To enable uniform load balancing across all servers, you set the `load_balance` property to `true` in the URL, as per the following example:

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                  user, password, host, port, dbname)
url := fmt.Sprintf("%s?load_balance=true", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

You can specify [multiple hosts](../../../drivers-orms/go/yb-pgx/#use-multiple-addresses) in the connection string in case the primary address fails. After the driver establishes the initial connection, it fetches the list of available servers from the universe, and performs load balancing of subsequent connection requests across these servers.

To specify topology keys, you set the `topology_keys` property to comma separated values, as per the following example:

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                  user, password, host, port, dbname)
url = fmt.Sprintf("%s?load_balance=true&topology_keys=cloud1.datacenter1.rack1", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

### Create table

Tables can be created in YugabyteDB by passing the `CREATE TABLE` DDL statement to the `Exec()` function on the `conn`instance.

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

```go
var createStmt = 'CREATE TABLE employee (id int PRIMARY KEY,
                  name varchar, age int, language varchar)';
_, err = conn.Exec(context.Background(), createStmt)
if err != nil {
  fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
}
```

The `conn.Exec()` function also returns an `error` object which, if not `nil`, needs to be handled in your code.

Read more on designing [Database schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the same `conn.Exec()` function.

```sql
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

```go
var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
                        " VALUES (1, 'John', 35, 'Go')";
_, err = conn.Exec(context.Background(), insertStmt)
if err != nil {
  fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
}
```

By default, the YugabyteDB PGX driver automatically prepares and caches statements.

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the function `conn.Query()`.

Query results are returned in `pgx.Rows`, which can be iterated using the `pgx.Rows.next()` method.

Then read the data using `pgx.rows.Scan()`.

The `SELECT` DML statement:

```sql
SELECT * from employee;
```

Code snippet:

```go
var name string
var age int
var language string

rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
if err != nil {
  log.Fatal(err)
}
defer rows.Close()

fmt.Printf("Query for id=1 returned: ");
for rows.Next() {
  err := rows.Scan(&name, &age, &language)
  if err != nil {
    log.Fatal(err)
  }
  fmt.Printf("Row[%s, %d, %s]\n", name, age, language)
}

err = rows.Err()
if err != nil {
  log.Fatal(err)
}
```

## Use pgxpool API

The YugabyteDB PGX driver also provides pool APIs via the `pgxpool` package. You can import it as follows:

```go
import (
  "github.com/yugabyte/pgx/v5/pgxpool"
)
```

### Establish a connection

The primary way of establishing a connection is with `pgxpool.New()`.

```go
pool, err := pgxpool.New(context.Background(), os.Getenv("DATABASE_URL"))
```

You can also provide configuration for the pool as follows:

```go
config, err := pgxpool.ParseConfig(os.Getenv("DATABASE_URL"))
if err != nil {
    // ...
}
config.AfterConnect = func(ctx context.Context, conn *pgx.Conn) error {
    // do something with every new connection
}

pool, err := pgxpool.NewWithConfig(context.Background(), config)
```

You can either `Acquire` a connection from the pool and execute queries on it, or use the Query API to directly execute SQLs on the pool.

```go
conn, err := pool.Acquire(context.Background())
if err != nil {
  fmt.Fprintf(os.Stderr, "Unable to connect to database: %v\n", err)
  os.Exit(1)
}
defer conn.Release()

var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                  name varchar, age int, language varchar)`
_, err = conn.Exec(context.Background(), createStmt)

// ...

rows, err := pool.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
```

For more details, see the [pgxpool package](https://pkg.go.dev/github.com/jackc/pgx/v5/pgxpool) documentation.

## Configure SSL/TLS

To build a Go application that communicates securely over SSL with YugabyteDB database, you need the root certificate (`ca.crt`) of the YugabyteDB cluster. To generate these certificates and install them while launching the cluster, follow the instructions in [Create server certificates](../../../secure/tls-encryption/server-certificates/).

Because a YugabyteDB Aeon cluster is always configured with SSL/TLS, you don't have to generate any certificate but only set the client-side SSL configuration. To fetch your root certificate, refer to [CA certificate](../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate).

For a YugabyteDB Aeon cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as follows at the client side.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Server CA Certificate |

### SSL modes

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | ------------------ |
| disable  | SSL Disabled | Supported
| allow    | SSL enabled only if server requires SSL connection | Supported
| prefer (default) | SSL enabled only if server requires SSL connection | Supported
| require | SSL enabled for data encryption and Server identity is not verified | Supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

## Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

The PGX driver provides the `conn.Begin()` function to start a transaction. The `conn.BeginEx()` function can create a transaction with a specified isolation level.

```go
tx, err := conn.Begin()
if err != nil {
    return err
}
defer tx.Rollback()

_, err = tx.Exec("insert into employee(id, name, age, language) values (1, 'John', 35, 'Go')")
if err != nil {
    return err
}

err = tx.Commit()
if err != nil {
    return err
}
```
