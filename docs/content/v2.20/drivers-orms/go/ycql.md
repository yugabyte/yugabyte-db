---
title: YugabyteDB Go driver for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Go application using YugabyteDB Go driver for YCQL
menu:
  v2.20:
    identifier: yb-pgx-driver-ycql
    parent: go-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../yb-pgx/" class="nav-link">
      YSQL
    </a>
  </li>
  <li class="active">
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB Go Driver
    </a>
  </li>
</ul>

[Yugabyte Go Driver for YCQL](https://github.com/yugabyte/gocql) is based on [gocql](https://gocql.github.io/) with additional [smart driver](../../smart-drivers-ycql/) features.

{{< note title="YugabyteDB Aeon" >}}

To use the driver's partition-aware load balancing feature in a YugabyteDB Aeon cluster, applications must be deployed in a VPC that has been peered with the cluster VPC so that they have access to all nodes in the cluster. For more information, refer to [Using YCQL drivers with YugabyteDB Aeon](../../smart-drivers-ycql/#using-ycql-drivers-with-yugabytedb-managed).

{{< /note >}}

## Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe and are able to interact with it using the YCQL shell. If not, follow these steps in [Quick start](/preview/quick-start/).
- installed Go version 1.13 or later.

## Install the Yugabyte Go Driver for YCQL

To install the [Yugabyte Go Driver for YCQL](https://github.com/yugabyte/gocql) locally, run the following command:

```sh
$ go get github.com/yugabyte/gocql
```

## Write the YCQL sample application

Create a file `ybcql_hello_world.go` and copy the contents below into it.

```go
package main;

import (
    "fmt"
    "log"
    "time"

    "github.com/yugabyte/gocql"
)

func main() {
    // Connect to the cluster.
    cluster := gocql.NewCluster("127.0.0.1", "127.0.0.2", "127.0.0.3")

    // Use the same timeout as the Java driver.
    cluster.Timeout = 12 * time.Second

    // Create the session.
    session, _ := cluster.CreateSession()
    defer session.Close()

    // Set up the keyspace and table.
    if err := session.Query("CREATE KEYSPACE IF NOT EXISTS ybdemo").Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created keyspace ybdemo")


    if err := session.Query(`DROP TABLE IF EXISTS ybdemo.employee`).Exec(); err != nil {
        log.Fatal(err)
    }
    var createStmt = `CREATE TABLE ybdemo.employee (id int PRIMARY KEY,
                                                           name varchar,
                                                           age int,
                                                           language varchar)`;
    if err := session.Query(createStmt).Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created table ybdemo.employee")

    // Insert into the table.
    var insertStmt string = "INSERT INTO ybdemo.employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')";
    if err := session.Query(insertStmt).Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)

    // Read from the table.
    var name string
    var age int
    var language string
    iter := session.Query(`SELECT name, age, language FROM ybdemo.employee WHERE id = 1`).Iter()
    fmt.Printf("Query for id=1 returned: ");
    for iter.Scan(&name, &age, &language) {
        fmt.Printf("Row[%s, %d, %s]\n", name, age, language)
    }

    if err := iter.Close(); err != nil {
        log.Fatal(err)
    }
}
```

## Run the application

To use the application, run the following `go run` command:

```sh
$ go run ybcql_hello_world.go
```

You should see the following as the output.

```output
Created keyspace ybdemo
Created table ybdemo.employee
Inserted data: INSERT INTO ybdemo.employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
Query for id=1 returned: Row[John, 35, Go]
```
