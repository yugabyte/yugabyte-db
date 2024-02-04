---
title: Go PGX driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Go application using PGX Driver
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.18:
    identifier: pgx-driver
    parent: go-drivers
    weight: 410
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
    <a href="../pgx/" class="nav-link active">
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

The [PGX driver](https://github.com/jackc/pgx/) is one of the most popular and actively maintained drivers for PostgreSQL. Use the driver to connect to YugabyteDB database to execute DML and DDL statements using the PGX APIs. It also supports the standard `database/sql` package.

## CRUD operations

For Go applications, most drivers provide database connectivity through the standard `database/sql` API.
The following sections break down the example to demonstrate how to perform common tasks required for Go application development using the PGX driver.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Import the driver package

Import the PGX driver package by adding the following import statement in your Go code.

```go
import (
  "github.com/jackc/pgx/v4"
)
```

To install the package locally, run the following commands:

```sh
mkdir yb-pgx
cd yb-pgx
go mod init hello
go get github.com/jackc/pgx/v4
```

### Step 2: Set up the database connection

Go applications can connect to the YugabyteDB database using the `pgx.Connect()` function. The `pgx` package includes all the common functions or structs required for working with YugabyteDB.

Use the `pgx.Connect()` method to create a connection object for the YugabyteDB database. This can be used to perform DDL and DML operations against the database.

The PGX connection URL is in the following format:

```sh
postgresql://username:password@hostname:port/database
```

Code snippet for connecting to YugabyteDB:

```go
url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
conn, err := pgx.Connect(context.Background(), url)
```

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| user | User connecting to the database | yugabyte
| password | User password | yugabyte
| host | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | Database name | yugabyte

#### Use SSL

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below at the client side. SSL/TLS is enabled by default for client-side authentication. Refer to [Configure SSL/TLS](../../../reference/drivers/go/pgx-reference/#configure-ssl-tls) for the default and supported modes.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment variable | Description |
| :------------------- | :---------- |
| PGSSLMODE | SSL mode used for the connection |
| PGSSLROOTCERT | Path to the root certificate on your computer |

### Step 3: Write your application

Create a file called `QuickStartApp.go` and add the following contents into it:

```go
package main

import (
  "context"
  "fmt"
  "log"
  "os"

  "github.com/jackc/pgx/v4"
)

const (
  host     = "127.0.0.1"
  port     = 5433
  user     = "yugabyte"
  password = "yugabyte"
  dbname   = "yugabyte"
)

func main() {
    // SSL/TLS config is read from env variables PGSSLMODE and PGSSLROOTCERT, if provided.
    url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                       user, password, host, port, dbname)
    conn, err := pgx.Connect(context.Background(), url)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Unable to connect to database: %v\n", err)
        os.Exit(1)
    }
    defer conn.Close(context.Background())

    var dropStmt = `DROP TABLE IF EXISTS employee`;

    _, err = conn.Exec(context.Background(), dropStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for drop table failed: %v\n", err)
    }
    // The `conn.Exec()` function also returns an `error` object which,
    // if not `nil`, needs to be handled in your code.
    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`;
    _, err = conn.Exec(context.Background(), createStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Println("Created table employee")

    // Insert data using the conn.Exec() function.
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    _, err = conn.Exec(context.Background(), insertStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)
    // The pgx driver automatically prepares and caches statements by default, so you don't have to.

    // Query data using the conn.Query() function with the SELECT statements.
    var name string
    var age int
    var language string
    rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    fmt.Printf("Query for id=1 returned: ");
    // Results are returned in pgx.Rows which can be iterated using the pgx.Rows.next() method.
    for rows.Next() {
        // Read the data using pgx.rows.Scan().
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
}
```

The **const** values are set to the defaults for a local installation of YugabyteDB. If you are using YugabyteDB Managed, replace the **const** values in the file as follows:

- **host** - The host address of your cluster. The host address is displayed on the cluster **Settings** tab.
- **user** - Your YugabyteDB database username. In YugabyteDB Managed, the default user is **admin**.
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
- [YugabyteDB PGX driver reference](../../../reference/drivers/go/pgx-reference/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- Build Go applications using [GORM](../gorm/)
- Build Go applications using [PG](../pg/)
