---
title: Go ORMs
linkTitle: Go ORMs
description: Go ORMs for YSQL
headcontent: Go ORMs for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    name: Go ORMs
    identifier: gorm
    parent: go-drivers
    weight: 400
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/drivers-orms/go/gorm/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      GORM ORM
    </a>
  </li>

  <li >
    <a href="/latest/drivers-orms/go/pg/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PG ORM
    </a>
  </li>

</ul>

[GORM](https://gorm.io/) is the ORM library for Golang.


## Quick start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using
the steps on the [Build an application](../../../quick-start/build-apps/go/ysql-gorm) page under the
Quick start section.

Let us break down the quick start example and understand how to perform the common tasks required
for Go App development using GORM.

## Step 1: Import the driver package

Import the GORM packages by adding the following import statement in your Go code.

```go
import (
  "github.com/jinzhu/gorm"
  _ "github.com/jinzhu/gorm/dialects/postgres"
)
```

## Step 2: Connect to YugabyteDB database

Go Apps can connect to the YugabyteDB database using the `gorm.Open()` function.

Use the `gorm.Open()` function for getting a connection to the YugabyteDB database which then can be
used for reading the data and writing the data into the database.

Code snippet for connecting to YugabyteDB:

```go
conn := fmt.Sprintf("host= %s port = %d user = %s password = %s dbname = %s sslmode=disable", host, port, user, password, dbname)
var err error
db, err = gorm.Open("postgres", conn)
defer db.Close()
if err != nil {
  panic(err)
}
```

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| dbname | database name | yugabyte

## Step 3: Create table

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

Read more on designing [Database schemas and tables](../../../../explore/ysql-language-features/databases-schemas-tables/).

## Step 4: Read and write data

To write data into YugabyteDB, use the `db.Create()` function.

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
