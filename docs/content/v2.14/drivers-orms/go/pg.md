---
title: Use an ORM
linkTitle: Use an ORM
description: Go ORM support for YugabyteDB
headcontent: Go ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: pg-orm
    parent: go-drivers
    weight: 400
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

[go-pg](https://github.com/go-pg/pg) is a Golang ORM for PostgreSQL.

## CRUD operations with PG ORM

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](../../../quick-start/build-apps/go/ysql-pg/) page under the Quick start section.

The following sections break down the quick start example to demonstrate how to perform common tasks required for Go application development using go-pg.

### Step 1: Import the ORM package

Import the PG packages by adding the following import statement in your Go code.

```go
import (
  "github.com/go-pg/pg/v10"
  "github.com/go-pg/pg/v10/orm"
)
```

### Step 2: Set up the database connection

Use the `pg.Connect()` function to establish a connection to the YugabyteDB database. This can then can be used to read and write data to the database.

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

### Step 3: Create tables

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

To write data to YugabyteDB, use the `Insert()` function.

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

## Next steps

- Explore [Scaling Go Applications](../../../explore/linear-scalability/) with YugabyteDB.
