---
title: Go Drivers
linkTitle: Go Drivers
description: Go Drivers for YSQL
headcontent: Go Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Go Drivers
    identifier: ref-yb-pgx-go-driver
    parent: drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="/preview/reference/drivers/go/yb-pgx-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
       YugabyteDB PGX Driver
    </a>
  </li>

  <li >
    <a href="/preview/reference/drivers/go/pgx-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="/preview/reference/drivers/go/pq-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

[YugabyteDB PGX driver](https://github.com/yugabyte/pgjdbc) is a Go driver for [YSQL](/preview/api/ysql/) based on [PGX driver](https://github.com/jackc/pgx/).

## Quick start

Learn how to establish a connection to YugabyteDB database and begin CRUD operations using the steps from [Build a Go application](../../../../quick-start/build-apps/go/ysql-yb-pgx/).

This page provides details for getting started with [YugabyteDB PGX driver](https://github.com/yugabyte/pgx) for connecting to YugabyteDB YSQL API.

## Import the driver package

You can import the YugabyteDB PGX driver package by adding the following import statement in your Go code.

```go
import (
  "github.com/yugabyte/pgx/v4"
)
```

Optionally, you can choose to import the pgxpool package instead. Refer to [Using pgxpool API](#using-pgxpool-api) to learn more.

## Fundamentals

Learn how to perform common tasks required for Go application development using the YugabyteDB PGX driver.

### Connect to YugabyteDB database

After setting up the driver, implement the Go client application that uses the driver to connect to your YugabyteDB cluster and run a query on the sample data.

The YugabyteDB PGX driver allows Go programmers to connect to YugabyteDB to execute DMLs and DDLs using the PGX APIs. It also supports the standard `database/sql` package.

Use the `pgx.Connect()` method or `pgxpool.Connect()` method to create a connection object for the YugabyteDB database. This can be used to perform DDLs and DMLs against the database.

The driver has the following features:

- It is **cluster-aware**, which eliminates the need for an external load balancer.

Connections are distributed across all the [YB-Tservers](/preview/architecture/concepts/yb-tserver/) in the cluster, irrespective of their placements.

The following table describes the connection parameters required to connect to the YugabyteDB database with **Uniform load balancing**.

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | Password for the user | yugabyte
| load_balance | enables uniform load balancing | true

To enable the cluster-aware connection load balancing, provide the parameter `load_balance=true` in the connection string as follows:

```go
"postgres://username:password@localhost:5433/database_name?load_balance=true"
```

- It is **topology-aware**, which is essential for geographically-distributed applications.

The following is a code snippet for connecting to YugabyteDB using the connection parameters.

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
url := fmt.Sprintf("%s?load_balance=true", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

Connections are distributed equally with the [YB-Tservers](/preview/architecture/concepts/yb-tserver/) in specific zones by specifying these zones as `topology_keys` with values in the format `cloud-name.region-name.zone-name`. Multiple zones can be specified with comma separated values.

The following table describes the connection parameters required to connect to the YugabyteDB database with **Topology-aware load balancing**.

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | database name | yugabyte
| load_balance | enables load balancing | true
| topology_keys | enables topology-aware load balancing | true

```sh
postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1,cloud1.region1.zone2
```

The following is a code snippet for connecting to YugabyteDB using the connection parameters.

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

The `conn.Exec()` function also returns an `error` object which, if not `nil`, needs to be handled
in your code.

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the same `conn.Exec()`
function.

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
Query results are returned in `pgx.Rows` which can be iterated using `pgx.Rows.next()` method.
Then read the data using `pgx.rows.Scan()`.

The SELECT DML statement:

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

### Using pgxpool API

The YugabyteDB PGX driver also provides pool APIs via the `pgxpool` package. You can import it as follows:

```go
import (
  "github.com/yugabyte/pgx/tree/master/pgxpool"
)
```

#### Establishing a connection

The primary way of establishing a connection is with `pgxpool.Connect()`.

```go
pool, err := pgxpool.Connect(context.Background(), os.Getenv("DATABASE_URL"))
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

pool, err := pgxpool.ConnectConfig(context.Background(), config)
```

For more details, see the [pgxpool package](https://pkg.go.dev/github.com/jackc/pgx/v4/pgxpool) documentation.

### Configure SSL/TLS

To build a Go application that communicates securely over SSL with YugabyteDB database,
you need the root certificate (`ca.crt`) of the YugabyteDB Cluster.
To generate these certificates and install them while launching the cluster, follow the instructions in
[Create server certificates](../../../../secure/tls-encryption/server-certificates/).

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related
environment variables as below at the client side.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Server CA Certificate |

#### SSL modes

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | ------------------ |
| disable  | SSL Disabled | Supported
| allow    | SSL enabled only if server requires SSL connection | Supported
| prefer (default) | SSL enabled only if server requires SSL connection | Supported
| require | SSL enabled for data encryption and Server identity is not verified | Supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

### Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB
supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for
maintaining strong consistency for concurrent data access.

The PGX driver provides the `conn.Begin()` function to start a transaction.
The `conn.BeginEx()` function can create a transaction with a specified isolation level.

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
