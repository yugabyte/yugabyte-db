---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go PostgreSQL driver and perform basic database operations.
menu:
  latest:
    parent: build-apps
    name: Go
    identifier: go-1
    weight: 552
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-pgx-ssl/" class="nav-link active">
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
    <a href="../ysql-pq-ssl/" class="nav-link">
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

The following tutorial creates a simple Go application that connects to a YugabyteDB cluster using the [pgx driver](https://pkg.go.dev/github.com/jackc/pgx), performs a few basic database operations — creating a table, inserting data, and running a SQL query — and then prints the results to the screen.

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within minutes by following the steps in [Quick start](../../../../quick-start/). Alternatively, you can use [Yugabyte Cloud](http://cloud.yugabyte.com/register), to get a fully managed database-as-a-service (DBaaS) for YugabyteDB.

- [Go version 1.15](https://golang.org/dl/), or later, is installed.

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

The [Go PostgreSQL driver package (`pgx`)](https://pkg.go.dev/github.com/jackc/pgx) is a Go driver and toolkit for the PostgreSQL. The current release of `pgx v4` requires Go modules.

To install the package locally, run the following commands:

```sh
$ mkdir yb-pgx
$ cd yb-pgx
$ go mod init hello
$ go get github.com/jackc/pgx/v4
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

    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`;
    _, err = conn.Exec(context.Background(), createStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Println("Created table employee")

    // Insert into the table.
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    _, err = conn.Exec(context.Background(), insertStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)

    // Read from the table.
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
}
```

## Disable SSL/TLS

For the default YugabyteDB setup, the SSL/TLS is not enabled. So, you don't need to specify any SSL-related configuration and directly [run the application](../ysql-pgx-ssl/#run-the-application).

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
