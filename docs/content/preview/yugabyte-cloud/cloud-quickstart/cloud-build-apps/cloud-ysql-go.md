---
title: Build a Go application that uses YSQL
headerTitle: Build a Go application
linkTitle: Go
description: Build a simple Go application using the Go PostgreSQL driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: Go PostgreSQL"
menu:
  preview_yugabyte-cloud:
    parent: cloud-build-apps
    name: Go
    identifier: cloud-go
    weight: 200
type: docs
---

The following tutorial shows a small [Go application](https://github.com/yugabyte/yugabyte-simple-go-app) that connects to a YugabyteDB cluster using the [Go PostgreSQL driver](../../../../reference/drivers/ysql-client-drivers/#go-postgresql-driver-pq) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Go.

## Prerequisites

In addition to [Go (tested with version 1.17.6)](https://go.dev/dl/), this tutorial requires the following.

### YugabyteDB Managed

- You have a cluster deployed in YugabyteDB Managed. To get started, use the [Quick start](../../).
- You downloaded the cluster CA certificate and added your computer to the cluster IP allow list. Refer to [Before you begin](../cloud-add-ip/).

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-go-app.git && cd yugabyte-simple-go-app
```

## Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `sample-app.go` file.

2. Set the following configuration parameter constants:

    - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
    - **dbName** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **sslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
    - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

First, initialize the `GO111MODULE` variable.

```sh
$ export GO111MODULE=auto
```

Import the Go PostgreSQL driver.

```sh
$ go get github.com/lib/pq
```

Start the application.

```sh
$ go run sample-app.go
```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Go application that works with YugabyteDB Managed.

## Explore the application logic

Open the `sample-app.go` file in the `yugabyte-simple-go-app` folder to review the methods.

### main

The `main` method establishes a connection with your cluster via the Go PostgreSQL driver.

```go
psqlInfo := fmt.Sprintf("host=%s port=%d user=%s password=%s dbname=%s",
    host, port, dbUser, dbPassword, dbName)

if sslMode != "" {
    psqlInfo += fmt.Sprintf(" sslmode=%s", sslMode)

    if sslRootCert != "" {
        psqlInfo += fmt.Sprintf(" sslrootcert=%s", sslRootCert)
    }
}

db, err := sql.Open("postgres", psqlInfo)
```

### createDatabase

The `createDatabase` method uses PostgreSQL-compliant DDL commands to create a sample database.

```go
Statement stmt = conn.createStatement();

stmt := `DROP TABLE IF EXISTS DemoAccount`
_, err := db.Exec(stmt)
checkIfError(err)

stmt = `CREATE TABLE DemoAccount (
                      id int PRIMARY KEY,
                      name varchar,
                      age int,
                      country varchar,
                      balance int)`

_, err = db.Exec(stmt)
checkIfError(err)

stmt = `INSERT INTO DemoAccount VALUES
              (1, 'Jessica', 28, 'USA', 10000),
              (2, 'John', 28, 'Canada', 9000)`

_, err = db.Exec(stmt)
checkIfError(err)
```

### selectAccounts

The `selectAccounts` method queries your distributed data using the SQL `SELECT` statement.

```go
rows, err := db.Query("SELECT name, age, country, balance FROM DemoAccount")
checkIfError(err)

defer rows.Close()

var name, country string
var age, balance int

for rows.Next() {
    err = rows.Scan(&name, &age, &country, &balance)
    checkIfError(err)

    fmt.Printf("name = %s, age = %v, country = %s, balance = %v\n",
        name, age, country, balance)
}
```

### transferMoneyBetweenAccounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```go
tx, err := db.Begin()
checkIfError(err)

_, err = tx.Exec(`UPDATE DemoAccount SET balance = balance - $1 WHERE name = 'Jessica'`, amount)
if checkIfTxAborted(err) {
    return
}
_, err = tx.Exec(`UPDATE DemoAccount SET balance = balance + $1 WHERE name = 'John'`, amount)
if checkIfTxAborted(err) {
    return
}

err = tx.Commit()
if checkIfTxAborted(err) {
    return
}
```

## Learn more

[Go PostgreSQL driver](../../../../reference/drivers/ysql-client-drivers/#go-postgresql-driver-pq)

[Explore more applications](../../../cloud-examples/)

[Deploy clusters in YugabyteDB Managed](../../../cloud-basics)

[Connect to applications in YugabyteDB Managed](../../../cloud-connect/connect-applications/)
