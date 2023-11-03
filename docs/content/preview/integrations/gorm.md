<!---
title: Using GORM with YugabyteDB
linkTitle: GORM
description: Using GORM with YugabyteDB
aliases:
menu:
  preview_integrations:
    identifier: gorm
    parent: integrations
    weight: 571
type: docs
--->

This document describes how to use [GORM](https://gorm.io/index.html), an object-relational mapping library for Golang, with YugabyteDB.

## Prerequisites

To use GORM with YugabyteDB, you need the following:

- YugabyteDB version 2.6 or later (see [Quick Start](../../quick-start/)).
- Python version 2.7 or later.
- Go version 1.8 or later.

## Configure GORM

You configure GORM as follows:

- Create a working directory by executing the following command:

  ```shell
  mkdir Demo-Gorm && cd Demo-Gorm
  ```

- Check your current `GOPATH` by executing the following command:

  ```bash
  echo $GOPATH
  ```

  If your `GOPATH` is not the absolute path to your working directory, set it as follows:

  ```shell
  export GOPATH=/<full-path-to-working-directory>
  ```

- Install the required GORM components by executing the following:

  ```shell
  go get github.com/lib/pq
  go get github.com/jinzhu/gorm
  go get github.com/jinzhu/gorm/dialects/postgres
  ```

## Use GORM

You can start using GORM with YugabyteDB as follows:

- Navigate to your working directory and create a `main.go` file.

- Add the following code to the `main.go` file:

  ```go
  package main

  import (
    "fmt"

   "github.com/jinzhu/gorm"
   _ "github.com/jinzhu/gorm/dialects/postgres"
  )

  type Employee struct {
   Id       int64  `gorm:"primary_key"`
   Name     string `gorm:"size:255"`
   Age      int64
   Language string `gorm:"size:255"`
  }

  const (
   host     = "localhost"
   port     = 5433
   user     = "yugabyte"
   password = "yugabyte"
   dbname   = "yugabyte"
  )

  var db *gorm.DB

  func main() {
   conn := fmt.Sprintf("host= %s port = %d user = %s password = %s dbname = %s sslmode=disable", host, port, user, password, dbname)
   var err error
   db, err = gorm.Open("postgres", conn)
   if err != nil {
     panic(err)
   }

   defer db.Close()

   // Create table
   db.Debug().AutoMigrate(&Employee{})

   // Insert value
   db.Create(&Employee{Id: 1, Name: "John", Age: 35, Language: "Golang-GORM"})
   db.Create(&Employee{Id: 2, Name: "Smith", Age: 24, Language: "Golang-GORM"})

   // Display input data
   var employees []Employee
   db.Find(&employees)
   for _, employee := range employees {
     fmt.Printf("Employee ID:%d\nName:%s\nAge:%d\nLanguage:%s\n", employee.Id, employee.Name, employee.Age, employee.Language)
     fmt.Printf("--------------------------------------------------------------\n")
   }

  }
  ```

- Configure YugabyteDB properties, as per your requirements. The default user and password for YugabyteDB is `yugabyte`, and the default port is 5433.

- Run GORM by executing the following command:

  ```shell
  go run main.go
  ```

If the execution is successful, the following will be displayed on your terminal:

![image](/images/ee/gorm1.png)

Another way to test the code is to open the ysqlsh client and execute the following statement:

```sql
yugabyte=# select * from employees;
```

Expect the following output:

```output
id  | name  | age | language
----+-------+-----+---------------
2   | Smith | 24  | Golang-GORM
1   | John  | 35  | Golang-GORM
(2 rows)
```
