---
title: Go Drivers
linkTitle: Go Drivers
description: Go Drivers for YSQL
headcontent: Go Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: Go Drivers
    identifier: pq-driver
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
    <a href="/latest/drivers-orms/go/pgx/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PGX Driver
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/go/pq/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [PQ driver](https://github.com/lib/pq/) is a popular driver for PostgreSQL which can used for
connecting to YugabyteDB YSQL as well.<br/>
This driver allows Go programmers to connect to YugabyteDB database to execute DMLs and DDLs using
the standard `database/sql` package.

## Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps in the [Build an application](/latest/quick-start/build-apps/go/ysql-pq) page under the
Quick start section.

Let us break down the quick start example and understand how to perform the common tasks required
for Go App development using the PQ driver.

## Step 1: Import the Driver Package

You can import the PQ driver package by adding the following import statement in your Go code.

### Import Statement

```golang
import (
  _ "github.com/lib/pq"
)
```

## Step 2: Connect to YugabyteDB

Go Apps can connect to the YugabyteDB database using the `sql.Open()` function.
All the functions or structs required for working with YugabyteDB database are part of `sql` package.

Use the `sql.Open()` function for getting connection object for the YugabyteDB database which can be
used for performing DDLs and DMLs against the database.

The connection details can be specified either as string params or via an url in the format given below:

```golang
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```golang
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

| Params | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| dbname | database name | yugabyte

## Step 3: Create Table

Execute an SQL statement like the DDL `CREATE TABLE ...` using the `Exec()` function on the `db`
instance.

The CREATE DDL statement:

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

Code snippet:

```golang
    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`;
    if _, err := db.Exec(createStmt); err != nil {
        log.Fatal(err)
    }
```

The `db.Exec()` function also returns an `error` object which, if not `nil`, needs to handled in
your code.

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

## Step 4: Read and Write Data

### Insert Data

In order to write data into YugabyteDB, execute the `INSERT` statement using the same `db.Exec()`
function.

The INSERT DML statement:

```java
INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
```

Code snippet:

```golang
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    if _, err := db.Exec(insertStmt); err != nil {
        log.Fatal(err)
    }
```

### Query Data

In order to query data from YugabyteDB tables, execute the `SELECT` statement using the function
`Query()` on `db` instance.

Query results are returned as `rows` which can be iterated using `rows.next()` method.
Use `rows.Scan()` for reading the data.

The SELECT DML statement:

```sql
SELECT * from employee;
```

Code snippet:

```golang
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

See this page - [PQ Driver](/latest/reference/drivers/go/pq-reference/) - for more details.
