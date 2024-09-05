---
title: PGX Driver
headerTitle: Go Drivers
linkTitle: Go Drivers
description: Go PGX Driver for YSQL
badges: ysql
aliases:
  - /preview/reference/drivers/go/pgx-reference/
menu:
  preview:
    name: Go Drivers
    identifier: ref-2-pgx-go-driver
    parent: go-drivers
    weight: 100
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../yb-pgx-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
       YugabyteDB PGX Smart Driver
    </a>
  </li>

  <li >
    <a href="../pgx-reference/" class="nav-link active">
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

The [PGX driver](https://github.com/jackc/pgx/) is one of the most popular and actively maintained drivers for PostgreSQL.

The driver allows Go programmers to connect to YugabyteDB to execute DMLs and DDLs using the PGX APIs. It also supports the standard `database/sql` package.

For a tutorial on building a sample Go application with PGX, see [Connect an application](../../../drivers-orms/go/pgx/).

## Fundamentals

Learn how to perform common tasks required for Go application development using the PGX driver.

### Import the driver package

You can import the PGX driver package by adding the following import statement in your Go code.

```go
import (
  "github.com/jackc/pgx/v4"
)
```

### Connect to YugabyteDB database

Go applications can connect to YugabyteDB using the `pgx.Connect()` function. The `pgx` package includes all the common functions or structs required for working with YugabyteDB.

Use the `pgx.Connect()` method to create the connection object, for performing DDLs and DMLs against the database.

The PGX Connection URL is in the following format:

```go
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```go
url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
conn, err := pgx.Connect(context.Background(), url)
```

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | database name | yugabyte

### Create table

Execute an SQL statement like the DDL `CREATE TABLE ...` using the `Exec()` function on the `conn` instance.

The CREATE DDL statement:

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

Code snippet:

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

The INSERT DML statement:

```sql
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

Code snippet:

```go
var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
                        " VALUES (1, 'John', 35, 'Go')";
_, err = conn.Exec(context.Background(), insertStmt)
if err != nil {
  fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
}
```

The PGX driver automatically prepares and caches statements by default, so that the developer does not have to.

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the function `conn.Query()`. Query results are returned in `pgx.Rows`, which can be iterated using `pgx.Rows.next()` method. Then read the data using `pgx.rows.Scan()`.

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

## Use pgxpool API

The PGX driver also provides pool APIs via its `pgxpool` package. Import it as follows:

```go
import (
  "github.com/jackc/pgx/v4/pgxpool"
)
```

### Establish a connection

The primary way to establish a connection is with `pgxpool.Connect()`.

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

For more details, see [pgxpool package doc](https://pkg.go.dev/github.com/jackc/pgx/v4/pgxpool).

## Configure SSL/TLS

To build a Go application that communicates securely over SSL with YugabyteDB database, you need the root certificate (`ca.crt`) of the YugabyteDB cluster.

To generate these certificates and install them while launching the cluster, follow the instructions in [Create server certificates](../../../secure/tls-encryption/server-certificates/).

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

Install [OpenSSL](https://www.openssl.org/) 1.1.1 or later only if you have a YugabyteDB setup with SSL/TLS enabled. YugabyteDB Aeon clusters are always SSL/TLS enabled.

The following table summarizes the SSL modes and their support in the driver:

| SSL Mode | Client Driver Behavior | YugabyteDB Support |
| :------- | :--------------------- | ------------------ |
| disable  | SSL Disabled | Supported
| allow    | SSL enabled only if server requires SSL connection | Supported
| prefer (default) | SSL enabled only if server requires SSL connection | Supported
| require | SSL enabled for data encryption and Server identity is not verified | Supported
| verify-ca | SSL enabled for data encryption and Server CA is verified | Supported
| verify-full | SSL enabled for data encryption. Both CA and hostname of the certificate are verified | Supported

YugabyteDB Aeon requires SSL/TLS, and connections using SSL mode `disable` will fail.

## Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

The PGX driver provides `conn.Begin()` function to start a transaction. The function `conn.BeginEx()` can create a transaction with a specified isolation level.`

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
