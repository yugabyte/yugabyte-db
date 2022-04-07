---
title: Go ORMs
linkTitle: Go ORMs
description: Go ORMs for YSQL
headcontent: Go ORMs for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Go ORMs
    identifier: go-pg
    parent: go-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/go/gorm/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      GORM ORM
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/go/pg/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG ORM
    </a>
  </li>

</ul>

[go-pg](https://github.com/go-pg/pg) is a Golang ORM for PostgreSQL.

## CRUD Operations with PG ORM

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps in the [Build an application](../../../quick-start/build-apps/go/ysql-pg) page under the
Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Go application development using go-pg.

### Step 1: Import the driver package

Import the PG packages by adding the following import statement in your Go code.

```go
import (
  "github.com/go-pg/pg/v10"
  "github.com/go-pg/pg/v10/orm"
)
```

### Step 2: Connect to YugabyteDB database

Go applications can connect to the YugabyteDB database using the `pg.Connect()` function.

Use the `pg.Connect()` function to establish a connection to the YugabyteDB database. This can then can be used to read and write data to the database.

Code snippet for connecting to YugabyteDB:

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
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| dbname | database name | yugabyte
| sslMode | SSL mode | require

### Step 3: Create table

Define a struct which maps to the table schema and use `AutoMigrate()` to create the table.

```go
type Employee struct {
    Id        int64
    Name      string
    Age       int64
    Language  []string
}
    ...
    model := (*Employee)(nil)
    err = db.Model(model).CreateTable(&orm.CreateTableOptions{
        Temp: false,
    })
    if err != nil {
        log.Fatal(err)
    }
```

Read more on designing [Database schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables/).

### Step 4: Read and write data

To write data into YugabyteDB, use the `Insert()` functions.

```go
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
```

To query data from YugabyteDB tables, execute the `Select()` function.

```go
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
```

## Next Steps

- Explore [Scaling Go Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Go applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-go/).