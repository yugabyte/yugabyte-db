---
title: YugabyteDB PGX Smart Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a Go application using YugabyteDB PGX Smart Driver
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  stable:
    identifier: yb-pgx-driver
    parent: go-drivers
    weight: 400
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
    <a href="../yb-pgx/" class="nav-link active">
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
    <a href="../pq/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PQ Driver
    </a>
  </li>

</ul>

The [YugabyteDB PGX smart driver](https://pkg.go.dev/github.com/yugabyte/pgx) is a Go driver for [YSQL](/preview/api/ysql/) based on [jackc/pgx](https://github.com/jackc/pgx/), with additional [connection load balancing](../../smart-drivers/) features.

The driver makes an initial connection to the first contact point provided by the application to discover all the nodes in the cluster. If the driver discovers stale information (by default, older than 5 minutes), it refreshes the list of live endpoints with every new connection attempt.

{{< note title="YugabyteDB Managed" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from a non-peered network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from non-peered networks fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Managed](../../smart-drivers/#using-smart-drivers-with-yugabytedb-managed).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for Go application development using the YugabyteDB PGX smart driver APIs.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Import the driver package

Import the YugabyteDB PGX driver package by adding the following import statement in your Go code:

```go
import (
  "github.com/yugabyte/pgx/v4"
)
```

To install the package locally, run the following commands:

```sh
mkdir yb-pgx
cd yb-pgx
go mod init hello
go get github.com/yugabyte/pgx/v4
go get github.com/yugabyte/pgx/v4/pgxpool # Install pgxpool package if you write your application with pgxpool.Connect().
```

Optionally, you can choose to import the pgxpool package instead. Refer to [Use pgxpool API](../../../reference/drivers/go/yb-pgx-reference/#use-pgxpool-api) to learn more.

### Step 2: Set up the database connection

Go applications can connect to the YugabyteDB database using the `pgx.Connect()` and `pgxpool.Connect()` functions. The `pgx` package includes all the common functions or structs required for working with YugabyteDB.

Use the `pgx.Connect()` method or `pgxpool.Connect()` method to create a connection object for the YugabyteDB database. This can be used to perform DDLs and DMLs against the database.

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| host | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). | localhost
| port |  Listen port for YSQL | 5433
| user | User connecting to the database | yugabyte
| password | User password | yugabyte
| dbname | Database name | yugabyte
| `load_balance` | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | Defaults to upstream driver behavior unless set to 'true'
| `yb_servers_refresh_interval` | If `load_balance` is true, the interval in seconds to refresh the servers list | 300
| `topology_keys` | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | If `load_balance` is true, uses uniform load balancing unless set to comma-separated geo-locations in the form `cloud.region.zone`.

The following is an example connection string for connecting to YugabyteDB with uniform load balancing:

```sh
postgres://username:password@localhost:5433/database_name?load_balance=true& \
    yb_servers_refresh_interval=240
```

The following is a code snippet for connecting to YugabyteDB using the connection parameters:

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
url := fmt.Sprintf("%s?load_balance=true&yb_servers_refresh_interval=240", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

The following is an example connection string for connecting to YugabyteDB with topology-aware load balancing, and including a fallback location:

```sh
postgres://username:password@localhost:5433/database_name?load_balance=true&topology_keys=cloud1.region1.zone1:1,cloud1.region1.zone2:2
```

The following is a code snippet for connecting to YugabyteDB using topology-aware load balancing:

```go
baseUrl := fmt.Sprintf("postgres://%s:%s@%s:%d/%s",
                    user, password, host, port, dbname)
url = fmt.Sprintf("%s?load_balance=true&topology_keys=cloud1.datacenter1.rack1", baseUrl)
conn, err := pgx.Connect(context.Background(), url)
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails.

{{< tip title="Tip">}}
To obtain a list of available hosts, you can connect to any cluster node and use the `yb_servers()` YSQL function.
{{< /tip >}}

Delimit the addresses using commas, as follows:

```sh
postgres://username:password@host1:5433,host2:5433,host3:5433/database_name?load_balance=true
```

The following is a code snippet for connecting to YugabyteDB using multiple hosts:

```go
url := fmt.Sprintf("postgres://%s:%s@%s:%d",
        dbUser, dbPassword, host1, port)

    if host2 != "" {
        url += fmt.Sprintf(",%s:%d", host2, port)
    }

    if host3 != "" {
        url += fmt.Sprintf(",%s:%d", host3, port)
    }

    url += fmt.Sprintf("/%s", dbName)

    if sslMode != "" {
        url += fmt.Sprintf("?sslmode=%s", sslMode)

        if sslRootCert != "" {
            url += fmt.Sprintf("&sslrootcert=%s", sslRootCert)
        }
    }
```

The hosts are only used during the initial connection attempt. If the first host is down when the driver is connecting, the driver attempts to connect to the next host in the string, and so on.

#### Use SSL

For a YugabyteDB Managed cluster, or a YugabyteDB cluster with SSL/TLS enabled, set the following SSL-related environment variables at the client side. SSL/TLS is enabled by default for client-side authentication. Refer to [Configure SSL/TLS](../../../reference/drivers/go/yb-pgx-reference/#configure-ssl-tls) for the default and supported modes.

```sh
$ export PGSSLMODE=verify-ca
$ export PGSSLROOTCERT=~/root.crt  # Here, the CA certificate file is downloaded as `root.crt` under home directory. Modify your path accordingly.
```

| Environment Variable | Description |
| :---------- | :---------- |
| PGSSLMODE |  SSL mode used for the connection |
| PGSSLROOTCERT | Path to the root certificate on your computer |

### Step 3 : Write your application with pgx.Connect()

Create a file called `QuickStart.go` and add the following contents into it:

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
    // The `conn.Exec()` function also returns an `error` object which,
    // if not `nil`, needs to be handled in your code.
    var createStmt = `CREATE TABLE employee (id int PRIMARY KEY,
                                             name varchar,
                                             age int,
                                             language varchar)`
    _, err = conn.Exec(context.Background(), createStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    fmt.Println("Created table employee")

    // Insert data using the conn.Exec() function.
    var insertStmt string = "INSERT INTO employee(id, name, age, language)" +
        " VALUES (1, 'John', 35, 'Go')"
    _, err = conn.Exec(context.Background(), insertStmt)
    if err != nil {
        fmt.Fprintf(os.Stderr, "Exec for create table failed: %v\n", err)
    }
    // The pgx driver automatically prepares and caches statements by default, so you don't have to.

    // Query data using the conn.Query() function with the SELECT statements.
    var name, language string
    var age int
    rows, err := conn.Query(context.Background(), "SELECT name, age, language FROM employee WHERE id = 1")
    if err != nil {
        log.Fatal(err)
    }
    defer rows.Close()
    // Results are returned in pgx.Rows which can be iterated using the pgx.Rows.next() method.
    for rows.Next() {
        // Read the data using pgx.rows.Scan().
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

- **host** - The host address of your cluster. The host address is displayed on the cluster **Settings** tab.
- **user** - Your YugabyteDB database username. In YugabyteDB Managed, the default user is **admin**.
- **password** - Your YugabyteDB database password.
- **dbname** - The name of the YugabyteDB database. The default name is **yugabyte**.
- **port** is set to 5433, which is the default port for the YSQL API.

Run the project `QuickStartApp.go` using the following command:

```go
go run QuickStartApp.go
```

This program expects your input to proceed through the application steps.

For a local cluster with three servers, all of them with the placement info `cloud1.datacenter1.rack1`, you should see the following output:

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

### Step 4 : Write your application with pgxpool.Connect()

Create a file called `QuickStart2.go` and add the following contents into it:

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

The **const** values are set to the defaults for a local installation of YugabyteDB. If you are using YugabyteDB Managed, replace the **const** values in the file as mentioned in [pgx.Connect()](#step-3-write-your-application-with-pgx-connect).

## Run the application

Run the project `QuickStartApp2.go` using the following command:

```go
go run QuickStartApp2.go
```

This program expects user input to proceed through the application steps.

For a local cluster with three servers, all of them with the placement info `cloud1.datacenter1.rack1`, you should see the following output:

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

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [YugabyteDB PGX smart driver reference](../../../reference/drivers/go/yb-pgx-reference/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- Build Go applications using [GORM](../gorm/)
- Build Go applications using [PG](../pg/)
