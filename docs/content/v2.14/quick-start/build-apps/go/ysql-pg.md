---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a sample Go application with the Go PostgreSQL driver and perform basic database operations with YugabyteDB.
menu:
  v2.14:
    parent: build-apps
    name: Go
    identifier: go-4
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
    <a href="../ysql-pg/" class="nav-link active">
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

The following tutorial creates a simple Go application that connects to a YugabyteDB cluster using the [Go-pg client and ORM](https://pkg.go.dev/github.com/go-pg/pg), performs a few basic database operations — creating a table, inserting data, and running a SQL query — and then prints the results to the screen.

## Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. If you are new to YugabyteDB, download, install, and start YugabyteDB by following the steps in [Quick start](../../../../quick-start/). Alternatively, you can use [YugabyteDB Managed](http://cloud.yugabyte.com/register) to get a fully managed database-as-a-service (DBaaS) for YugabyteDB.

- [Go version 1.8](https://golang.org/dl/), or later, is installed.

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
| prefer | Supported |
| require | Supported |
| verify-ca | Supported |
| verify-full | Supported |

YugabyteDB Managed requires SSL/TLS, and connections using SSL mode `disable` will fail.

### Go PostgreSQL driver

[Go-pg/pg](https://github.com/go-pg/pg) is an ORM for Golang applications working with PostgreSQL. The current release of `pg v10` requires Go modules.

To install the package locally, run the following commands:

```sh
$ mkdir yb-go-pg
$ cd yb-go-pg
$ go mod init hello
$ go get github.com/go-pg/pg/v10
```

## Create the sample Go application

Create a file `ybsql_hello_world.go` and copy the contents below.

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

The **const** values are set to the defaults for a local installation of YugabyteDB. If you are using YugabyteDB Managed, replace the **const** values in the file as follows:

- **host** - The host address of your cluster. The host address is displayed on the cluster Settings tab.
- **user** - Your Yugabyte database username. In YugabyteDB Managed, the default user is **admin**.
- **password** - Your Yugabyte database password.
- **dbname** - The name of the Yugabyte database. The default Yugabyte database name is **yugabyte**.

**port** is set to 5433, which is the default port for the YSQL API.

### Using pg.Options()

If the password contains these special characters (#, %, ^), the driver may fail to parse the URL. In such a case, use pg.Options() instead of pg.ParseURL() to initialize the Options in `ybsql_hello_world.go`. The standard PG environment variables except PGPASSWORD and PGSSLROOTCERT are implicitly read by the driver. Set the PG variables as follows (replace the values as appropriate for YugabyteDB Managed):

```sh
$ export PGHOST=127.0.0.1
$ export PGPORT=5433
$ export PGUSER=yugabyte
$ export PGPASSWORD=password#with%special^chars
$ export PGDATABASE=yugabyte
```

To use pg.Options(), replace the main function in your file with the following:

```go
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
Created table
Inserted data
Query for id=1 returned: Employee<1 John 35 [%!l(string=Go)]>
```
