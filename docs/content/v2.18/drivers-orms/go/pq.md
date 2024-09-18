---
title: Go PQ Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Go application using PQ driver
menu:
  v2.18:
    identifier: pq-driver
    parent: go-drivers
    weight: 420
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../yb-pgx/" class="nav-link">
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
    <a href="../yb-pgx/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB PGX Smart Driver
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

## CRUD operations

For Go Applications, most drivers provide database connectivity through the standard `database/sql` API.
The following sections break down the example to demonstrate how to perform common tasks required for Go application development using the PQ driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Import the driver package

Import the PQ driver package by adding the following import statement in your Go code.

```go
import (
  _ "github.com/lib/pq"
)
```

To install the package locally, run the following commands:

{{< note title="Note">}}
Set the  environment variable `GO111MODULE` before installing the `lib/pq` package if your Go version is 1.11 or higher.
{{< /note >}}

```sh
export GO111MODULE=auto
go get github.com/lib/pq
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
| :-------- | :---------- | :------ |
| user | User connecting to the database | yugabyte
| password | User password | yugabyte
| host | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | Database name | yugabyte

#### Use SSL

For a YugabyteDB Aeon cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below at the client side. SSL/TLS is enabled by default for client-side authentication. Refer to [Configure SSL/TLS](../../../reference/drivers/go/pq-reference/#ssl-modes) for the default and supported modes.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Path to the root certificate on your computer |

### Step 3: Write your application

Create a file called `QuickStart.go` and add the following contents into it:

```go
package main

import (
  "database/sql"
  "fmt"
  "log"

  _ "github.com/lib/pq"
)

const (
  host     = "127.0.0.1"
  port     = 5433
  user     = "yugabyte"
  password = "yugabyte"
  dbname   = "yugabyte"
)

func main() {
    psqlInfo := fmt.Sprintf("host=%s port=%d user=%s password=%s dbname=%s",
                            host, port, user, password, dbname)
    // Other connection configs are read from the standard environment variables:
    // PGSSLMODE, PGSSLROOTCERT, and so on.
    db, err := sql.Open("postgres", psqlInfo)
    if err != nil {
        log.Fatal(err)
    }

    var dropStmt = `DROP TABLE IF EXISTS employee`;
    if _, err := db.Exec(dropStmt); err != nil {
        log.Fatal(err)
    }
    // The `conn.Exec()` function also returns an `error` object which,
    // if not `nil`, needs to be handled in your code.
    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`;
    if _, err := db.Exec(createStmt); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created table employee")

    // Insert data using the conn.Exec() function.
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    if _, err := db.Exec(insertStmt); err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)

    // Execute the `SELECT` statement using the function `Query()` on `db` instance.
    var name string
    var age int
    var language string
    rows, err := db.Query(`SELECT name, age, language FROM employee WHERE id = 1`)
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    fmt.Printf("Query for id=1 returned: ");
    // Results are returned as `rows` which can be iterated using `rows.next()` method.
    for rows.Next() {
        // Use `rows.Scan()` for reading the data.
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

    defer db.Close()
}
```

The **const** values are set to the defaults for a local installation of YugabyteDB. If you're using YugabyteDB Aeon, replace the values as follows:

- **host** - The host address of your cluster. The host address is displayed on the cluster **Settings** tab.
- **user** - Your YugabyteDB database username. In YugabyteDB Aeon, the default user is **admin**.
- **password** - Your YugabyteDB database password.
- **dbname** - The name of the YugabyteDB database. The default name is **yugabyte**.
- **port** is set to 5433, which is the default port for the YSQL API.

Run the project `QuickStartApp.go` using the following command:

```go
go run QuickStartApp.go
```

You should see output similar to the following:

```output
Created table employee
Inserted data: INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
Query for id=1 returned: Row[John, 35, Go]
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [YugabyteDB PQ driver reference](../../../reference/drivers/go/pq-reference/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- Build Go applications using [GORM](../gorm/)
- Build Go applications using [PG](../pg/)
