---
title: Build a C application that uses YSQL
headerTitle: Build a C application
description: Build a small C application using the libpq driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: libpq"
menu:
  preview_yugabyte-cloud:
    parent: cloud-build-apps
    name: C
    identifier: cloud-c
    weight: 500
type: docs
---

The following tutorial shows a small [C application](https://github.com/yugabyte/yugabyte-simple-c-app) that connects to a YugabyteDB cluster using the [libpq driver](../../../../reference/drivers/ysql-client-drivers/#libpq) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C.

## Prerequisites

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq to establish secure SSL connections).
- [libpq](../../../../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).

### YugabyteDB Managed

- You have a cluster deployed in YugabyteDB Managed. To get started, use the [Quick start](../../).
- You downloaded the cluster CA certificate and added your computer to the cluster IP allow list. Refer to [Before you begin](../cloud-add-ip/).

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-c-app.git && cd yugabyte-simple-c-app
```

## Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `sample-app.c` file.

2. Set the following configuration-related macros:

    - **HOST** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **PORT** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
    - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `verify-full`.
    - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

Build the application with gcc or clang.

```sh
gcc sample-app.c -o sample-app -I<path-to-libpq>/libpq/include -L<path-to-libpq>/libpq/lib -lpq
```

Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

Start the application.

```sh
$ ./sample-app
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

You have successfully executed a basic C application that works with YugabyteDB Managed.

## Explore the application logic

Open the `sample-app.c` file in the `yugabyte-simple-c-app` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the libpq driver.

```cpp
PQinitSSL(1);

conn = PQconnectdb(CONN_STR);

if (PQstatus(conn) != CONNECTION_OK) {
    printErrorAndExit(conn, NULL);
}

printf(">>>> Successfully connected to YugabyteDB!\n");

return conn;
```

### createDatabase

The `createDatabase` method uses PostgreSQL-compliant DDL commands to create a sample database.

```cpp
res = PQexec(conn, "DROP TABLE IF EXISTS DemoAccount");

if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}

res = PQexec(conn, "CREATE TABLE DemoAccount ( \
                    id int PRIMARY KEY, \
                    name varchar, \
                    age int, \
                    country varchar, \
                    balance int)");

if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}

res = PQexec(conn, "INSERT INTO DemoAccount VALUES \
                    (1, 'Jessica', 28, 'USA', 10000), \
                    (2, 'John', 28, 'Canada', 9000)");

if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}
```

### selectAccounts

The `selectAccounts` method queries your distributed data using the SQL `SELECT` statement.

```cpp
res = PQexec(conn, "SELECT name, age, country, balance FROM DemoAccount");

if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    printErrorAndExit(conn, res);
}

for (i = 0; i < PQntuples(res); i++) {
    printf("name = %s, age = %s, country = %s, balance = %s\n",
        PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2), PQgetvalue(res, i, 3));
}
```

### transferMoneyBetweenAccounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```cpp
res = PQexec(conn, "BEGIN TRANSACTION");
if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}

res = PQexec(conn, "UPDATE DemoAccount SET balance = balance - 800 WHERE name = \'Jessica\'");
if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}

res = PQexec(conn, "UPDATE DemoAccount SET balance = balance + 800 WHERE name = \'John\'");
if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}

res = PQexec(conn, "COMMIT");
if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    printErrorAndExit(conn, res);
}
```

## Learn more

[libpq driver](../../../../reference/drivers/ysql-client-drivers/#libpq)

[Explore more applications](../../../cloud-examples/)

[Deploy clusters in YugabyteDB Managed](../../../cloud-basics)

[Connect to applications in YugabyteDB Managed](../../../cloud-connect/connect-applications/)
