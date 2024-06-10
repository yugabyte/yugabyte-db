---
title: Connect an app
linkTitle: Connect an app
description: Go drivers for YSQL
menu:
  v2.14:
    identifier: yb-pgx-driver
    parent: go-drivers
    weight: 400
type: docs
---

For Go applications, most drivers provide database connectivity through the standard `database/sql` API. YugabyteDB supports the [PGX Driver](https://github.com/jackc/pgx) and the [PQ Driver](https://github.com/lib/pq).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../yb-pgx/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB PGX Driver
    </a>
  </li>
  <li >
    <a href="../pgx/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="../pq/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [YugabyteDB PGX driver](https://pkg.go.dev/github.com/yugabyte/pgx) is a Go driver based on [jackc/pgx](https://github.com/jackc/pgx/), with an additional feature of [Connection load balancing](https://github.com/yugabyte/pgx#connection-load-balancing).

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/go/ysql-yb-pgx/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Go application development using the YugabyteDB PGX driver.

### Step 1: Import the driver package

Import the YugabyteDB PGX driver package by adding the following import statement in your Go code:

```go
import (
  "github.com/yugabyte/pgx/v4"
)
```

Optionally, you can choose to import the pgxpool package instead. Refer to [Using pgxpool API](../../../reference/drivers/go/yb-pgx-reference/#using-pgxpool-api) to learn more.

### Step 2: Set up the database connection

Go applications can connect to the YugabyteDB database using the `pgx.Connect()` and `pgxpool.Connect()` functions. The `pgx` package includes all the common functions or structs required for working with YugabyteDB.

Use the `pgx.Connect()` method or `pgxpool.Connect()` method to create a connection object for the YugabyteDB database. This can be used to perform DDLs and DMLs against the database.

The following table describes the connection parameters required to connect to the YugabyteDB database with **Uniform load balancing**.

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | database name | yugabyte
| load_balance | enables uniform load balancing | true
| topology_keys | enables topology-aware load balancing | true

The following is an example connection string for connecting to YugabyteDB with uniform load balancing.

```sh
postgres://username:password@localhost:5433/database_name?load_balance=true
```

The following is a code snippet for connecting to YugabyteDB using the connection parameters.

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
url := fmt.Sprintf("%s?load_balance=true", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

The following table describes the connection parameters required to connect to the YugabyteDB database with **Topology-aware load balancing**.

The following is an example connection string for connecting to YugabyteDB with topology-aware load balancing.

```sh
postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1,cloud1.region1.zone2
```

The following is a code snippet for connecting to YugabyteDB using topology-aware load balancing.

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
url = fmt.Sprintf("%s?load_balance=true&topology_keys=cloud1.datacenter1.rack1", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

#### Use SSL

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the following SSL-related environment variables at the client side. SSL/TLS is enabled by default for client-side authentication. Refer to [Configure SSL/TLS](../../../quick-start/build-apps/go/ysql-pgx/#ssl-tls-configuration) for the default and supported modes.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Path to the root certificate on your computer |

### Step 3: Create tables

Execute an SQL statement like the DDL `CREATE TABLE` using the `Exec()` function on the `conn` instance.

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

### Step 4: Read and write data

#### Insert data

To write data to YugabyteDB, execute the `INSERT` statement using the same `conn.Exec()` function.

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

The pgx driver automatically prepares and caches statements by default, so you don't have to.

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the function `conn.Query()`. Query results are returned in `pgx.Rows` which can be iterated using the `pgx.Rows.next()` method. Then read the data using `pgx.rows.Scan()`.

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

## Next steps

- Learn how to build Go applications using [GORM](../gorm/).
- Learn more about [fundamentals](../../../reference/drivers/go/yb-pgx-reference/#fundamentals) of the YugabyteDB PGX Driver.
