---
title: Use an ORM
linkTitle: Use an ORM
description: Go ORM support for YugabyteDB
headcontent: Go ORM support for YugabyteDB
menu:
  v2.16:
    identifier: go-orm
    parent: go-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../gorm/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      GORM ORM
    </a>
  </li>

  <li >
    <a href="../pg/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG ORM
    </a>
  </li>

</ul>

[GORM](https://gorm.io/) is the ORM library for Golang.

## CRUD operations

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the steps on the [Go ORM example application](../../orms/go/ysql-gorm/) page.

The following sections break down the example to demonstrate how to perform common tasks required for Go application development using GORM.

### Step 1: Import the ORM package

Import the GORM packages by adding the following import statement in your application's `main.go` code.

```go
import (
  "github.com/jinzhu/gorm"
  _ "github.com/jinzhu/gorm/dialects/postgres"
)
```

### Step 2: Set up the database connection

Go applications can connect to the YugabyteDB database using the `gorm.Open()` function.

```go
conn := fmt.Sprintf("host= %s port = %d user = %s password = %s dbname = %s sslmode=disable", host, port, user, password, dbname)
var err error
db, err = gorm.Open("postgres", conn)
defer db.Close()
if err != nil {
  panic(err)
}
```

| Parameter | Description | Default |
| :---------- | :---------- | :------ |
| host  | Hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| user | User connecting to the database | yugabyte
| password | Password for connecting to the database | yugabyte
| dbname | Database name | yugabyte

### Step 3: Create a table

Define a struct which maps to the table schema and use `AutoMigrate()` to create the table.

```go
type Employee struct {
  Id       int64  `gorm:"primary_key"`
  Name     string `gorm:"size:255"`
  Age      int64
  Language string `gorm:"size:255"`
}
 ...

// Create table
db.Debug().AutoMigrate(&Employee{})
```

Read more on designing [Database schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables/).

### Step 4: Read and write data

To write data to YugabyteDB, use the `db.Create()` function.

```go
// Insert value
db.Create(&Employee{Id: 1, Name: "John", Age: 35, Language: "Golang-GORM"})
db.Create(&Employee{Id: 2, Name: "Smith", Age: 24, Language: "Golang-GORM"})
```

To query data from YugabyteDB tables, use the `db.Find()` function.

```go
// Display input data
var employees []Employee
db.Find(&employees)
for _, employee := range employees {
  fmt.Printf("Employee ID:%d\nName:%s\nAge:%d\nLanguage:%s\n", employee.Id, employee.Name, employee.Age, employee.Language)
  fmt.Printf("--------------------------------------------------------------\n")
}
```

## Learn more

- Build Go applications using [PG](../pg/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
