---
title: Go Drivers
linkTitle: Go Drivers
description: Go Drivers for YSQL
headcontent: Go Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: Go Drivers
    identifier: pgx-driver
    parent: go-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---
For Go Applications, most drivers provide database connectivity through the standard `database/sql`
API. YugabyteDB supports [PGX Driver](https://github.com/jackc/pgx) and the
[PQ Driver](https://github.com/lib/pq).

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/go/pgx/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/go/pq/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [PGX driver](https://github.com/jackc/pgx/) is the most popular and actively maintained driver
for PostgreSQL which can used for connecting to YugabyteDB YSQL as well.
This driver allows Go programmers to connect to YugabyteDB database to execute DMLs and DDLs using
the PGX APIs. It also supports the standard `database/sql` package.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps on the [Build an application](/latest/quick-start/build-apps/go/ysql-pgx) page under the
Quick start section.

## Step 1: Import the Driver Package

You can import the PGX driver package by adding the following import statement in your Go code.

### Import Statement

```golang
import (
  "github.com/jackc/pgx/v4"
)
```

## Fundamentals

Let us learn how to perform the common tasks required for Go App development using the PGX driver.

## Step 2: Connect to YugabyteDB

Go Apps can connect to the YugabyteDB database using the `pgx.Connect()` function.
All the functions or structs required for working with YugabyteDB database will be part of `pgx` package.

Use the `pgx.Connect()` method for getting connection object for the YugabyteDB database which can be
used for performing DDLs and DMLs against the database.

PGX Connection url is in the format given below:

```golang
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```golang
url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
conn, err := pgx.Connect(context.Background(), url)
```

| Params | Description | Default |
| :---------- | :---------- | :------ |
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | database name | yugabyte

### Create Table

Execute an SQL statement like the DDL `CREATE TABLE ...` using the `Exec()` function on the `conn`
instance.

DDL statement:

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

Code snippet:

```golang
var createStmt = 'CREATE TABLE employee (id int PRIMARY KEY,
                  name varchar, age int, language varchar)';
_, err = conn.Exec(context.Background(), createStmt)
if err != nil {
  fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
}
```

The `conn.Exec()` function also returns an `error` object which, if not `nil`, needs to handled in
your code.

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

## Step 3:  Read and Write Data

#### Insert Data

In order to write data into YugabyteDB, execute the `INSERT` statement using the same `conn.Exec()`
function.

INSERT DML:

```java
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

Code snippet:

```golang
var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
                        " VALUES (1, 'John', 35, 'Go')";
_, err = conn.Exec(context.Background(), insertStmt)
if err != nil {
  fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
}
```

TODO prepared statement?

```golang
// TODO
```

#### Query Data

In order to query data from YugabyteDB tables, execute the `SELECT` statement using the function
`Query()` on `conn` instance.
Query results are returned using `java.sql.ResultSet` interface which can be iterated using `resultSet.next()` method for reading the data. Read more on [ResultSet](https://docs.oracle.com/javase/7/docs/api/java/sql/ResultSet.html).

For example

```sql
SELECT * from employee;
```

```golang
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

TODO (More features?)

