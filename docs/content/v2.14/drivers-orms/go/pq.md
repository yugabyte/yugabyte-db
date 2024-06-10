---
title: Connect an app
linkTitle: Connect an app
description: Go drivers for YSQL
menu:
  v2.14:
    identifier: pq-driver
    parent: go-drivers
    weight: 420
type: docs
---

For Go Applications, most drivers provide database connectivity through the standard `database/sql` API. YugabyteDB supports the [PGX Driver](https://github.com/jackc/pgx) and the [PQ Driver](https://github.com/lib/pq).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../yb-pgx/" class="nav-link">
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
    <a href="../pq/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [PQ driver](https://github.com/lib/pq/) is a popular driver for PostgreSQL. Use the driver to connect to YugabyteDB to execute DMLs and DDLs using the standard `database/sql` package.

## CRUD operations with PQ driver

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/go/ysql-pq/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Go application development using the PQ driver.

### Step 1: Import the driver package

Import the PQ driver package by adding the following import statement in your Go code.

```go
import (
  _ "github.com/lib/pq"
)
```

### Step 2: Set up the database connection

Go applications can connect to YugabyteDB using the `sql.Open()` function. The `sql` package includes all the functions or structs required for working with YugabyteDB.

Use the `sql.Open()` function to create a connection object for the YugabyteDB database. This can be used for performing DDLs and DMLs against the database.

The connection details can be specified either as string parameters or via a URL in the following format:

```sh
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```go
psqlInfo := fmt.Sprintf("host=%s port=%d user=%s password=%s dbname=%s",
                        host, port, user, password, dbname)
// Other connection configs are read from the standard environment variables:
// PGSSLMODE, PGSSLROOTCERT, and so on.
db, err := sql.Open("postgres", psqlInfo)
defer db.Close()
if err != nil {
    log.Fatal(err)
}
```

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| dbname | database name | yugabyte

#### Use SSL

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below at the client side. SSL/TLS is enabled by default for client-side authentication. Refer to [OpenSSL](../../../quick-start/build-apps/go/ysql-pq/#openssl) for the default and supported modes.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Path to the root certificate on your computer |

### Step 3: Create tables

Execute an SQL statement such as DDL `CREATE TABLE ...` using the `Exec()` function on the `db` instance.

The CREATE DDL statement:

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

Code snippet:

```go
var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                         name varchar,
                                         age int,
                                         language varchar)`;
if _, err := db.Exec(createStmt); err != nil {
    log.Fatal(err)
}
```

The `db.Exec()` function also returns an `error` object which, if not `nil`, needs to be handled in your code.

Read more on designing [Database schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables/).

### Step 4: Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the same `db.Exec()` function.

The INSERT DML statement:

```sql
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

Code snippet:

```go
var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
    " VALUES (1, 'John', 35, 'Go')";
if _, err := db.Exec(insertStmt); err != nil {
    log.Fatal(err)
}
```

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the function `Query()` on `db` instance.

Query results are returned as `rows` which can be iterated using `rows.next()` method. Use `rows.Scan()` for reading the data.

The SELECT DML statement:

```sql
SELECT * from employee;
```

Code snippet:

```go
var name string
var age int
var language string
rows, err := db.Query(`SELECT name, age, language FROM employee WHERE id = 1`)
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
- Learn more about [fundamentals](../../../reference/drivers/go/pq-reference/) of the PQ Driver.
