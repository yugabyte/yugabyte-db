---
title: Build a Go application that uses YSQL over SSL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go PostgreSQL driver and perform basic database operations with YugabyteDB via SSL/TLS.
menu:
  stable:
    parent: build-apps
    name: Go
    identifier: go-3
    weight: 552
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
 <li>
    <a href="../ysql-pgx-ssl/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PGX - SSL
    </a>
  </li>
  <li >
    <a href="../ysql-pq/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PQ
    </a>
  </li>
  <li >
    <a href="../ysql-pq-ssl/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PQ - SSL
    </a>
  </li>
  <li >
    <a href="../ysql-pg-ssl/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PG - SSL
    </a>
  </li>
  <li >
    <a href="../ysql-gorm/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - GORM
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The following tutorial creates a simple Go application that connects to a YugabyteDB cluster using the [pq driver](https://godoc.org/github.com/lib/pq), performs a few basic database operations — creating a table, inserting data, and running a SQL query — and then prints the results to the screen.

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within minutes by following the steps in [Quick start](../../../../quick-start/). Alternatively, you can use [Yugabyte Cloud](http://cloud.yugabyte.com/register), to get a fully managed database-as-a-service (DBaaS) for YugabyteDB.

- [Go version 1.8](https://golang.org/dl/), or later, is installed.

### SSL/TLS configuration

Refer to the [SSL/TLS configuration](../../../../secure/tls-encryption/client-to-server/) page to launch your YugabyteDB cluster with SSL/TLS enabled. This is already taken care of for you in **Yugabyte Cloud**, as the cluster is pre-configured to do client-side authentication with SSL/TLS enabled.

### OpenSSL

Install [OpenSSL](https://www.openssl.org/) 1.1.1 or later only if you have a YugabyteDB setup with SSL/TLS enabled. The **Yugabyte Cloud** clusters are always SSL/TLS enabled.

### CA certificate

Download the CA certificate file to allow your application connect securely to the YugabyteDB cluster. In case of a **Yugabyte Cloud** cluster,

1. On the **Clusters** tab, select a cluster.

1. Click **Connect**

1. Click **Connect to your application** and download the CA cert.

### Go PostgreSQL driver

The [Go PostgreSQL driver package (`pq`)](https://godoc.org/github.com/lib/pq) is a Go PostgreSQL driver for the `database/sql` package.

To install the package locally, run the following commands:

{{< note title="Note">}}
The environment variable `GO111MODULE` may need to be set, if your Go version is 1.11 or higher. It is set before installing the lib/pq package.
{{< /note >}}

```sh
$ export GO111MODULE=auto
$ go get github.com/lib/pq
```

## Create the sample Go application

Create a file `ybsql_hello_world.go` and copy the contents below.

{{< note title="Note">}}
The constants defined under the `const` block in the code below have the default values.
You may need to change the values of `host`, `user` and `password` as per your YugabyteDB cluster setup.
For a **Yugabyte Cloud** cluster, the `host` will have a value in the format similar to `xxxx-xxxx-xxxx-xxxx-xxxx.aws.ybdb.io`.
{{< /note >}}

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

    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`;
    if _, err := db.Exec(createStmt); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created table employee")

    // Insert into the table.
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    if _, err := db.Exec(insertStmt); err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)

    // Read from the table.
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

    defer db.Close()
}
```

## Enable SSL/TLS

For the **Yugabyte Cloud** cluster or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below.

   ```sh
    $ export PGSSLMODE=verify-ca
    $ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
   ```

## Run the application

   ```sh
     $ go run ybsql_hello_world.go
   ```

   You should see the following output.

   ```output
     Created table employee
     Inserted data: INSERT INTO employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
     Query for id=1 returned: Row[John, 35, Go]
   ```
