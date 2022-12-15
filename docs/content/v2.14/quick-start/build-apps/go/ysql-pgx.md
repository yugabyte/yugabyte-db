---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go PostgreSQL driver and perform basic database operations.
menu:
  v2.14:
    parent: build-apps
    name: Go
    identifier: go-2
    weight: 552
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-yb-pgx/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - YugabyteDB PGX
    </a>
  </li>
  <li>
    <a href="../ysql-pgx/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PGX
    </a>
  </li>
  <li >
    <a href="../ysql-pq/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PQ
    </a>
  </li>
  <li >
    <a href="../ysql-pg/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PG
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

- YugabyteDB is up and running. If you are new to YugabyteDB, you can download, install, and have YugabyteDB up and running within minutes by following the steps in [Quick start](../../../../quick-start/). Alternatively, you can use [YugabyteDB Managed](http://cloud.yugabyte.com/register) to get a fully managed database-as-a-service (DBaaS) for YugabyteDB.

- [Go version 1.15](https://golang.org/dl/), or later, is installed.

### SSL/TLS configuration

You can choose to enable or disable SSL for your local YugabyteDB cluster. Refer [here](../../../../secure/tls-encryption/client-to-server/) to learn about configuring SSL/TLS for your YugabyteDB cluster. YugabyteDB Managed requires SSL connections, and SSL/TLS is enabled by default for client-side authentication.

#### CA certificate

Use the  [CA certficate](../../../../secure/tls-encryption/server-certificates/#generate-the-root-certificate-file) generated above as part of the SSL/TLS configuration of your cluster.

In case of a YugabyteDB Managed cluster, to download the CA certificate for your cluster in YugabyteDB Managed, do the following:

1. On the **Clusters** tab, select a cluster.

1. Click **Connect**.

1. Click **Connect to your application** and download the CA cert.

#### OpenSSL

Install [OpenSSL](https://www.openssl.org/) 1.1.1 or later only if you have a YugabyteDB setup with SSL/TLS enabled. YugabyteDB Managed clusters are always SSL/TLS enabled.

The following table summarizes the SSL modes and their support in the driver:

| SSL Mode | Client driver behavior |
| :--------- | :---------------- |
| disable | Supported |
| allow | Supported |
| prefer (default) | Supported |
| require | Supported |
| verify-ca | Supported |
| verify-full | Supported |

YugabyteDB Managed requires SSL/TLS, and connections using SSL mode `disable` will fail.

### Go PostgreSQL driver

The [Go PostgreSQL driver package (`pgx`)](https://pkg.go.dev/github.com/jackc/pgx) is a Go driver and toolkit for PostgreSQL. The current release of `pgx v4` requires Go modules.

To install the package locally, run the following commands:

```sh
$ mkdir yb-pgx
$ cd yb-pgx
$ go mod init hello
$ go get github.com/jackc/pgx/v4
```

## Create the sample Go application

Create a file `ybsql_hello_world.go` and copy the contents below.

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

The **const** values are set to the defaults for a local installation of YugabyteDB. If you are using YugabyteDB Managed, replace the **const** values in the file as follows:

- **host** - The host address of your cluster. The host address is displayed on the cluster Settings tab.
- **user** - Your Yugabyte database username. In YugabyteDB Managed, the default user is **admin**.
- **password** - Your Yugabyte database password.
- **dbname** - The name of the Yugabyte database. The default Yugabyte database name is **yugabyte**.

**port** is set to 5433, which is the default port for the YSQL API.

## Enable SSL/TLS

For a YugabyteDB Managed cluster or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below.

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
