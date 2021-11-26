---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go PostgreSQL driver and perform basic database operations.
menu:
  stable:
    parent: build-apps
    name: Go
    identifier: go-4
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
    <a href="../ysql-pq-ssl/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL - PQ - SSL
    </a>
  </li>
  <li >
    <a href="../ysql-pg-ssl/" class="nav-link active">
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

The following tutorial creates a simple Go application that connects to a YugabyteDB cluster using the [Go-pg client and ORM](https://pkg.go.dev/github.com/go-pg/pg), performs a few basic database operations — creating a table, inserting data, and running a SQL query — and then prints the results to the screen.

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

The [Go-pg/pg](https://github.com/go-pg/pg) is an ORM for Golang applications working with Postgresql. The current release of `pg v10` requires Go modules.

To install the package locally, run the following commands:

```sh
$ mkdir yb-go-pg
$ cd yb-go-pg
$ go mod init hello
$ go get github.com/go-pg/pg/v10
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
  "fmt"
  "log"
  "os"
  "crypto/tls"
  "crypto/x509"
  "io/ioutil"
  "github.com/go-pg/pg/v10"
  "github.com/go-pg/pg/v10/orm"
)

type Employee struct {
    Id        int64
    Name      string
    Age       int64
    Language  []string
}

const (
  host     = "127.0.0.1"
  port     = 5433
  user     = "yugabyte"
  password = "yugabyte"
  dbname   = "yugabyte"
)

func (u Employee) String() string {
    return fmt.Sprintf("Employee<%d %s %v %l>", u.Id, u.Name, u.Age, u.Language)
}

func main() {
    var sslMode = ""
    var ssl = os.Getenv("PGSSLMODE")
    if ssl != "" {
        sslMode = "?sslmode=" + ssl
    }

    url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s%s",
                      user, password, host, port, dbname, sslMode)
    opt, errors := pg.ParseURL(url)
    if errors != nil {
        log.Fatal(errors)
    }

    CAFile := os.Getenv("PGSSLROOTCERT")
    if (CAFile != "") {
        CACert, err2 := ioutil.ReadFile(CAFile)
        if err2 != nil {
            log.Fatal(err2)
        }

        CACertPool := x509.NewCertPool()
        CACertPool.AppendCertsFromPEM(CACert)

        tlsConfig := &tls.Config{
          RootCAs:            CACertPool,
          ServerName:         host,
        }
        opt.TLSConfig = tlsConfig
    }
    db := pg.Connect(opt)

    defer db.Close()

    model := (*Employee)(nil)
    err := db.Model(model).DropTable(&orm.DropTableOptions{
        IfExists: true,
    })
    if err != nil {
        log.Fatal(err)
    }

    err = db.Model(model).CreateTable(&orm.CreateTableOptions{
        Temp: false,
    })
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Created table")

    // Insert into the table.
    employee1 := &Employee{
        Name:   "John",
        Age:    35,
        Language: []string{"Go"},
    }
    _, err = db.Model(employee1).Insert()
    if err != nil {
        log.Fatal(err)
    }

    _, err = db.Model(&Employee{
        Name:      "Kelly",
        Age:       35,
        Language:  []string{"Golang", "Python"},
    }).Insert()
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Inserted data")

    // Read from the table.
    emp := new(Employee)
    err = db.Model(emp).
        Where("employee.id = ?", employee1.Id).
        Select()
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Query for id=1 returned: ");
    fmt.Println(emp)
}
```

{{< note title="Note">}}
If the `password` contains these special characters (#, %, ^), the driver may fail to parse the url. In such a case, use pg.Options() instead of pg.ParseURL() to initialize the Options in `ybsql_hello_world.go`. The standard PG environment variables except `PGPASSWORD` and `PGSSLROOTCERT` are implicitly read by the driver.
{{< /note >}}

```sh
/* Modify the main() from the ybsql_hello_world.go script by replacing the first few lines and enabling pg.Options() */

func main() {
    opt := &pg.Options{
        Password: os.Getenv("PGPASSWORD"),
    }

    CAFile := os.Getenv("PGSSLROOTCERT")
    if (CAFile != "") {
        CACert, err2 := ioutil.ReadFile(CAFile)
        if err2 != nil {
            log.Fatal(err2)
        }

        CACertPool := x509.NewCertPool()
        CACertPool.AppendCertsFromPEM(CACert)

        tlsConfig := &tls.Config{
          RootCAs:            CACertPool,
          ServerName:         host,
        }
        opt.TLSConfig = tlsConfig
    }
    db := pg.Connect(opt)

    defer db.Close()

    model := (*Employee)(nil)
    err := db.Model(model).DropTable(&orm.DropTableOptions{
        IfExists: true,
    })
    if err != nil {
        log.Fatal(err)
    }

    err = db.Model(model).CreateTable(&orm.CreateTableOptions{
        Temp: false,
    })
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Created table")

    // Insert into the table.
    employee1 := &Employee{
        Name:   "John",
        Age:    35,
        Language: []string{"Go"},
    }
    _, err = db.Model(employee1).Insert()
    if err != nil {
        log.Fatal(err)
    }

    _, err = db.Model(&Employee{
        Name:      "Kelly",
        Age:       35,
        Language:  []string{"Golang", "Python"},
    }).Insert()
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Inserted data")

    // Read from the table.
    emp := new(Employee)
    err = db.Model(emp).
        Where("employee.id = ?", employee1.Id).
        Select()
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Query for id=1 returned: ");
    fmt.Println(emp)
}
```

## Disable SSL/TLS

For the default YugabyteDB setup, the SSL/TLS is not enabled. So, you don't need to specify any SSL-related configuration and directly [run the application](../ysql-pg-ssl/#run-the-application).

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
    Created table
    Inserted data
    Query for id=1 returned: Employee<1 John 35 [%!l(string=Go)]>
   ```
