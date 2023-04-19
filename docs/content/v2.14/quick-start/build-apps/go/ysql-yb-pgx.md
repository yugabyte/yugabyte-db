---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go YugabyteDB driver and perform basic database operations.
menu:
  v2.14:
    parent: build-apps
    name: Go
    identifier: go-1
    weight: 552
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-yb-pgx/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - YugabyteDB PGX
    </a>
  </li>
  <li>
    <a href="../ysql-pgx/" class="nav-link">
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

This driver is a fork of the [jackc/pgx](https://github.com/jackc/pgx) driver, with an added feature of [connection load balancing](https://github.com/yugabyte/pgx#connection-load-balancing).

In this tutorial, you'll create a small Go application that connects to a YugabyteDB cluster using the [YugabyteDB PGX driver](https://pkg.go.dev/github.com/yugabyte/pgx), performs a few basic database operations — creating a table, inserting data, and running a SQL query — and prints the results to the screen.

The first example demonstrates connection load balancing using the `pgx.Connect()` API, while the second uses the `pgxpool.Connect()` API.

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you're new to YugabyteDB, you can download, install, and have YugabyteDB up and running within minutes by following the steps in [Quick start](../../../../quick-start/). Alternatively, you can use [YugabyteDB Managed](http://cloud.yugabyte.com/) to get a fully managed database-as-a-service (DBaaS) for YugabyteDB.

- [Go version 1.15](https://golang.org/dl/) or later is installed.

### SSL/TLS configuration

The YugabyteDB PGX driver SSL/TLS configuration is the same as for the [YSQL PGX](../ysql-pgx/#ssl-tls-configuration) Go driver.

### Go YugabyteDB driver

The current release of the [Go YugabyteDB driver package (`pgx`)](https://pkg.go.dev/github.com/yugabyte/pgx/v4) is v4, and requires Go modules.

To install the package locally, run the following commands:

```sh
mkdir yb-pgx
cd yb-pgx
go mod init hello
go get github.com/yugabyte/pgx/v4
go get github.com/yugabyte/pgx/v4/pgxpool
```

## Write an application with pgx.Connect() {#pgx-connect-sample}

Create a file called `ybsql_hello_world.go` and paste the following contents into it:

```go
package main

import (
    "bufio"
    "context"
    "fmt"
    "log"
    "os"
    "strconv"
    "time"

    "github.com/yugabyte/pgx/v4"
)

const (
    host     = "localhost"
    port     = 5433
    user     = "yugabyte"
    password = "yugabyte"
    dbname   = "yugabyte"
    numconns = 12
)

var connCloseChan chan int = make(chan int)
var baseUrl string = fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
    user, password, host, port, dbname)

func main() {
    // Create a table and insert a row
    url := fmt.Sprintf("%s?load_balance=true", baseUrl)
    fmt.Printf("Connection url: %s\n", url)
    createTable(url)
    printAZInfo()
    pause()

    fmt.Println("---- Demonstrating uniform (cluster-aware) load balancing ----")
    executeQueries(url)
    fmt.Println("You can verify the connection counts on http://127.0.0.1:13000/rpcz and similar urls for other servers.")
    pause()
    closeConns(numconns)

    fmt.Println("---- Demonstrating topology-aware load balancing ----")
    url = fmt.Sprintf("%s?load_balance=true&topology_keys=cloud1.datacenter1.rack1", baseUrl)
    fmt.Printf("Connection url: %s\n", url)
    executeQueries(url)
    pause()
    closeConns(numconns)

    fmt.Println("Closing the application ...")
}

func closeConns(num int) {
    fmt.Printf("Closing %d connections ...\n", num)
    for i := 0; i < num; i++ {
        connCloseChan <- i
    }
}

func pause() {
    reader := bufio.NewReader(os.Stdin)
    fmt.Print("\nPress Enter/return to proceed: ")
    reader.ReadString('\n')
}

func createTable(url string) {
    conn, err := pgx.Connect(context.Background(), url)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Unable to connect to database: %v\n", err)
        os.Exit(1)
    }
    defer conn.Close(context.Background())

    var dropStmt = `DROP TABLE IF EXISTS employee`
    _, err = conn.Exec(context.Background(), dropStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for drop table failed: %v\n", err)
    }

    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`
    _, err = conn.Exec(context.Background(), createStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Println("Created table employee")

    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')"
    _, err = conn.Exec(context.Background(), insertStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }

    // Read from the table.
    var name, language string
    var age int
    rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    for rows.Next() {
        err := rows.Scan(&name, &age, &language)
        if err != nil {
            log.Fatal(err)
        }
        // log.Printf("Row[%s, %d, %s]\n", name, age, language)
    }
    err = rows.Err()
    if err != nil {
        log.Fatal(err)
    }
}

func executeQueries(url string) {
    fmt.Printf("Creating %d connections ...\n", numconns)
    for i := 0; i < numconns; i++ {
        go executeQuery("GO Routine "+strconv.Itoa(i), url, connCloseChan)
    }
    time.Sleep(5 * time.Second)
    printHostLoad()
}

func executeQuery(grid string, url string, ccChan chan int) {
    conn, err := pgx.Connect(context.Background(), url)
    if err != nil {
        fmt.Fprintf(os.Stderr, "[%s] Unable to connect to database: %v\n", grid, err)
        os.Exit(1)
    }

    // Read from the table.
    var name, language string
    var age int
    rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    fstr := fmt.Sprintf("[%s] Query for id=1 returned: ", grid)
    for rows.Next() {
        err := rows.Scan(&name, &age, &language)
        if err != nil {
            log.Fatal(err)
        }
        fstr = fstr + fmt.Sprintf(" Row[%s, %d, %s]", name, age, language)
    }
    err = rows.Err()
    if err != nil {
        log.Fatal(err)
    }
    // log.Println(fstr)
    _, ok := <-ccChan
    if ok {
        conn.Close(context.Background())
    }
}

func printHostLoad() {
    for k, cli := range pgx.GetHostLoad() {
        str := "Current load on cluster (" + k + "): "
        for h, c := range cli {
            str = str + fmt.Sprintf("\n%-30s:%5d", h, c)
        }
        fmt.Println(str)
    }
}

func printAZInfo() {
    for k, zl := range pgx.GetAZInfo() {
        str := "Placement info details of cluster (" + k + "): "
        for z, hosts := range zl {
            str = str + fmt.Sprintf("\n    AZ [%s]: ", z)
            for _, s := range hosts {
                str = str + fmt.Sprintf("%s, ", s)
            }
        }
        fmt.Println(str)
    }
}
```

The **const** values are set to the defaults for a local installation of YugabyteDB. If you're using YugabyteDB Managed, replace the values as follows:

- **host** - The host address of your cluster. The host address is displayed on the cluster Settings tab.
- **user** - Your Yugabyte database username. In YugabyteDB Managed, the default user is **admin**.
- **password** - Your Yugabyte database password.
- **dbname** - The name of the Yugabyte database. The default Yugabyte database name is **yugabyte**.

**port** is set to 5433, which is the default port for the YSQL API.

### Enable SSL/TLS

For a YugabyteDB Managed cluster or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as below.

```sh
export PGSSLMODE=verify-ca
export PGSSLROOTCERT=~/root.crt
# In this example, the CA certificate file is downloaded as `root.crt` in your home directory. Modify your path accordingly.
```

### Run the application

```sh
$ go run ybsql_hello_world.go
```

This program expects user input to proceed through the application steps.

For a local cluster with three servers, all of them with the placement info `cloud1.datacenter1.rack1`, you would see the following output:

```output
Connection url: postgres://yugabyte:yugabyte@localhost:5433/yugabyte?load_balance=true
Created table employee
Placement info details of cluster (127.0.0.1):
    AZ [cloud1.datacenter1.rack1]: 127.0.0.3, 127.0.0.2, 127.0.0.1,

Press Enter/return to proceed:
---- Demonstrating uniform (cluster-aware) load balancing ----
Creating 12 connections ...
Current load on cluster (127.0.0.1):
127.0.0.3                     :    4
127.0.0.2                     :    4
127.0.0.1                     :    4
You can verify the connection counts on http://127.0.0.1:13000/rpcz and similar urls for other servers.

Press Enter/return to proceed:
Closing 12 connections ...
---- Demonstrating topology-aware load balancing ----
Connection url: postgres://yugabyte:yugabyte@localhost:5433/yugabyte?load_balance=true&topology_keys=cloud1.datacenter1.rack1
Creating 12 connections ...
Current load on cluster (127.0.0.1):
127.0.0.1                     :    4
127.0.0.3                     :    4
127.0.0.2                     :    4

Press Enter/return to proceed:
Closing 12 connections ...
Closing the application ...
```

## Write an application with pgxpool.Connect() {#pgxpool-connect-sample}

Create a file called `ybsql_hello_world_pool.go` and paste the following contents into it:

```go
package main

import (
    "bufio"
    "context"
    "fmt"
    "log"
    "os"
    "strconv"
    "sync"
    "time"

    "github.com/yugabyte/pgx/v4"
    "github.com/yugabyte/pgx/v4/pgxpool"
)

const (
    host     = "localhost"
    port     = 5433
    user     = "yugabyte"
    password = "yugabyte"
    dbname   = "yugabyte"
    numconns = 12
)

var pool *pgxpool.Pool
var wg sync.WaitGroup
var baseUrl string = fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
    user, password, host, port, dbname)

func main() {
    // Create a table and insert a row
    url := fmt.Sprintf("%s?load_balance=true", baseUrl)
    initPool(url)
    defer pool.Close()
    createTableUsingPool(url)
    printAZInfo()
    pause()

    fmt.Println("---- Demonstrating uniform (cluster-aware) load balancing ----")
    executeQueriesOnPool()
    fmt.Println("You can verify the connection counts on http://127.0.0.1:13000/rpcz and similar urls for other servers.")
    pause()
    pool.Close()

    // Create the pool with a placement zone specified as topology_keys
    fmt.Println("---- Demonstrating topology-aware load balancing ----")
    url = fmt.Sprintf("%s?load_balance=true&topology_keys=cloud1.datacenter1.rack1", baseUrl)
    initPool(url)
    executeQueriesOnPool()
    pause()
    pool.Close()
    fmt.Println("Closing the application ...")
}

func initPool(url string) {
    var err error
    fmt.Printf("Initializing pool with url %s\n", url)
    pool, err = pgxpool.Connect(context.Background(), url)
    if err != nil {
        log.Fatalf("Error initializing the pool: %s", err.Error())
    }
}

func pause() {
    reader := bufio.NewReader(os.Stdin)
    fmt.Print("\nPress Enter/return to proceed: ")
    reader.ReadString('\n')
}

func createTableUsingPool(url string) {
    fmt.Println("Creating table using pool.Acquire() ...")
    conn, err := pool.Acquire(context.Background())
    if err != nil {
        fmt.Fprintf(os.Stderr, "Unable to connect to database: %v\n", err)
        os.Exit(1)
    }
    defer conn.Release()

    var dropStmt = `DROP TABLE IF EXISTS employee`
    _, err = conn.Exec(context.Background(), dropStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for drop table failed: %v\n", err)
    }

    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`
    _, err = conn.Exec(context.Background(), createStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Println("Created table employee")

    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')"
    _, err = conn.Exec(context.Background(), insertStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    // fmt.Printf("Inserted data: %s\n", insertStmt)

    // Read from the table.
    var name, language string
    var age int
    rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    for rows.Next() {
        err := rows.Scan(&name, &age, &language)
        if err != nil {
            log.Fatal(err)
        }
    }
    err = rows.Err()
    if err != nil {
        log.Fatal(err)
    }
}

func executeQueriesOnPool() {
    fmt.Printf("Acquiring %d connections from pool ...\n", numconns)
    for i := 0; i < numconns; i++ {
        wg.Add(1)
        go executeQueryOnPool("GO Routine " + strconv.Itoa(i))
    }
    time.Sleep(1 * time.Second)
    wg.Wait()
    printHostLoad()
}

func executeQueryOnPool(grid string) {
    defer wg.Done()
    for {
        // Read from the table.
        var name, language string
        var age int
        rows, err := pool.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
        if err != nil {
            log.Fatalf("pool.Query() failed, %s", err)
        }
        defer rows.Close()
        fstr := fmt.Sprintf("[%s] Query for id=1 returned: ", grid)
        for rows.Next() {
            err := rows.Scan(&name, &age, &language)
            if err != nil {
                log.Fatalf("rows.Scan() failed, %s", err)
            }
            fstr = fstr + fmt.Sprintf(" Row[%s, %d, %s] ", name, age, language)
        }
        err = rows.Err()
        if err != nil {
            fmt.Printf("%s, retrying ...\n", err)
            continue
        }
        time.Sleep(5 * time.Second)
        break
    }
}

func printHostLoad() {
    for k, cli := range pgx.GetHostLoad() {
        str := "Current load on cluster (" + k + "): "
        for h, c := range cli {
            str = str + fmt.Sprintf("\n%-30s:%5d", h, c)
        }
        fmt.Println(str)
    }
}

func printAZInfo() {
    for k, zl := range pgx.GetAZInfo() {
        str := "Placement info details of cluster (" + k + "): "
        for z, hosts := range zl {
            str = str + fmt.Sprintf("\n    AZ [%s]: ", z)
            for _, s := range hosts {
                str = str + fmt.Sprintf("%s, ", s)
            }
        }
        fmt.Println(str)
    }
}
```

The **const** values are set to the defaults for a local installation of YugabyteDB. If you are using YugabyteDB Managed, replace the **const** values in the file as [explained earlier](#pgx-connect-sample).

### Enable SSL/TLS

For a YugabyteDB Managed cluster or a YugabyteDB cluster with SSL/TLS enabled, set the SSL-related environment variables as follows.

```sh
export PGSSLMODE=verify-ca
export PGSSLROOTCERT=~/root.crt
# In this example, the CA certificate file is downloaded as `root.crt` in your home directory.  Modify your path accordingly.
```

### Run the application

```sh
$ go run ybsql_hello_world_pool.go
```

This program expects user input to proceed through the application steps.

For a local cluster with three servers, all of them with the placement info `cloud1.datacenter1.rack1`, you would see the following output:

```output
Initializing pool with url postgres://yugabyte:yugabyte@localhost:5433/yugabyte?load_balance=true
Creating table using pool.Acquire() ...
Created table employee
Placement info details of cluster (127.0.0.1):
    AZ [cloud1.datacenter1.rack1]: 127.0.0.3, 127.0.0.2, 127.0.0.1,

Press Enter/return to proceed:
---- Demonstrating uniform (cluster-aware) load balancing ----
Acquiring 12 connections from pool ...
Current load on cluster (127.0.0.1):
127.0.0.3                     :    4
127.0.0.2                     :    4
127.0.0.1                     :    4
You can verify the connection counts on http://127.0.0.1:13000/rpcz and similar urls for other servers.

Press Enter/return to proceed:
---- Demonstrating topology-aware load balancing ----
Initializing pool with url postgres://yugabyte:yugabyte@localhost:5433/yugabyte?load_balance=true&topology_keys=cloud1.datacenter1.rack1
Acquiring 12 connections from pool ...
Current load on cluster (127.0.0.1):
127.0.0.3                     :    4
127.0.0.2                     :    4
127.0.0.1                     :    4

Press Enter/return to proceed:
Closing the application ...
```
