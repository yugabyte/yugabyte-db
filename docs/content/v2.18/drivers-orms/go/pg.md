---
title: PG ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Go PG ORM support for YugabyteDB
headcontent: Go ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.18:
    identifier: pg-orm
    parent: go-drivers
    weight: 610
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../gorm/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      GORM ORM
    </a>
  </li>

  <li >
    <a href="../pg/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG ORM
    </a>
  </li>

</ul>

[go-pg](https://github.com/go-pg/pg) is an ORM for Golang applications with PostgreSQL.

## CRUD operations

The following sections break down the example to demonstrate how to perform common tasks required for Go application development using [go-pg client and ORM](https://pkg.go.dev/github.com/go-pg/pg).

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Import the ORM package

The current release of `pg v10` requires Go modules. Import the `pg` packages by adding the following import statement in your Go code.

```go
import (
  "github.com/go-pg/pg/v10"
  "github.com/go-pg/pg/v10/orm"
)
```

To install the package locally, run the following commands:

```sh
$ mkdir yb-go-pg
$ cd yb-go-pg
$ go mod init hello
$ go get github.com/go-pg/pg/v10
```

### Step 2: Set up the database connection

Use the `pg.Connect()` function to establish a connection to the YugabyteDB database. This can be used to read and write data to the database.

```go
url := fmt.Sprintf("postgres://%s:%s@%s:%d/%s%s",
                  user, password, host, port, dbname, sslMode)
opt, errors := pg.ParseURL(url)
if errors != nil {
    log.Fatal(errors)
}

db := pg.Connect(opt)
```

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| user | User for connecting to the database | yugabyte |
| password | User password | yugabyte |
| host  | Hostname of the YugabyteDB instance | localhost |
| port |  Listen port for YSQL | 5433 |
| dbname | Database name | yugabyte |
| sslMode | SSL mode | require |

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

The driver supports all the SSL modes. YugabyteDB Managed requires SSL/TLS, and connections using SSL mode `disable` will fail.

### Step 3: Write your application

Create a file `ybsql_hello_world.go` and copy the following:

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

// Define a struct which maps to the table schema
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

    // Insert into the table using the Insert() function.
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

    // Read from the table using the Select() function.
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

#### Using pg.Options()

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

#### Run the application

Run the application using the following command:

```sh
go run ybsql_hello_world.go
```

You should see output similar to the following:

```output
Created table
Inserted data
Query for id=1 returned: Employee<1 John 35 [%!l(string=Go)]>
```

## Learn more

- Build Go applications using [GORM](../gorm/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
