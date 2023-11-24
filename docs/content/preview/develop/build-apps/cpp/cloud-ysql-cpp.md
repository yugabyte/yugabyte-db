---
title: Build a C++ application using the libpqxx driver
headerTitle: Build a C++ application
description: Build a small C++ application using the libpqxx driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: libpqxx"
aliases:
  - /preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-cpp/
menu:
  preview_tutorials:
    parent: build-apps
    name: C++
    identifier: cloud-cpp
    weight: 500
type: docs
---

The following tutorial shows a small [C++ application](https://github.com/yugabyte/yugabyte-simple-cpp-app) that connects to a YugabyteDB cluster using the [libpqxx driver](../../../../reference/drivers/ysql-client-drivers/#libpqxx) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C++.

## Prerequisites

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq and libpqxx to establish secure SSL connections).
- [libpq](../../../../reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [libpqxx](../../../../reference/drivers/ysql-client-drivers/#libpqxx). Homebrew users on macOS can install using `brew install libpqxx`. To build the driver yourself, refer to [Building libpqxx](https://github.com/jtv/libpqxx#building-libpqxx).

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-cpp-app.git && cd yugabyte-simple-cpp-app
```

## Provide connection parameters

If your cluster is running on YugabyteDB Managed, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `sample-app.cpp` file.

2. Set the following configuration-related constants:

    - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **DB_NAME** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.
    - **SSL_MODE** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `verify-full`.
    - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

Build the application with gcc or clang.

```sh
g++ -std=c++17 sample-app.cpp -o sample-app -lpqxx -lpq \
-I<path-to-libpq>/libpq/include -I<path-to-libpqxx>/libpqxx/include \
-L<path-to-libpq>/libpq/lib -L<path-to-libpqxx>/libpqxx/lib
```

Replace `<path-to-libpq>` with the path to the libpq installation, and `<path-to-libpqxx>` with the path to the libpqxx installation; for example, `/usr/local/opt`.

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

You have successfully executed a basic C++ application that works with YugabyteDB Managed.

## Explore the application logic

Open the `sample-app.cpp` file in the `yugabyte-simple-cpp-app` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the libpqxx driver.

```cpp
std::string url = "host=" + HOST + " port=" + PORT + " dbname=" + DB_NAME +
    " user=" + USER + " password=" + PASSWORD;

if (SSL_MODE != "") {
    url += " sslmode=" + SSL_MODE;

    if (SSL_ROOT_CERT != "") {
        url += " sslrootcert=" + SSL_ROOT_CERT;
    }
}

std::cout << ">>>> Connecting to YugabyteDB!" << std::endl;

pqxx::connection *conn = new pqxx::connection(url);

std::cout << ">>>> Successfully connected to YugabyteDB!" << std::endl;
```

### createDatabase

The `createDatabase` method uses PostgreSQL-compliant DDL commands to create a sample database.

```cpp
pqxx::work txn(*conn);

txn.exec("DROP TABLE IF EXISTS DemoAccount");

txn.exec("CREATE TABLE DemoAccount ( \
            id int PRIMARY KEY, \
            name varchar, \
            age int, \
            country varchar, \
            balance int)");

txn.exec("INSERT INTO DemoAccount VALUES \
            (1, 'Jessica', 28, 'USA', 10000), \
            (2, 'John', 28, 'Canada', 9000)");

txn.commit();
```

### selectAccounts

The `selectAccounts` method queries your distributed data using the SQL `SELECT` statement.

```cpp
res = txn.exec("SELECT name, age, country, balance FROM DemoAccount");

for (auto row: res) {
    std::cout
        << "name=" << row["name"].c_str() << ", "
        << "age=" << row["age"].as<int>() << ", "
        << "country=" << row["country"].c_str() << ", "
        << "balance=" << row["balance"].as<int>() << std::endl;
}
```

### transferMoneyBetweenAccounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```cpp
try {
    pqxx::work txn(*conn);

    txn.exec("UPDATE DemoAccount SET balance = balance -" + std::to_string(amount)
        + " WHERE name = \'Jessica\'");

    txn.exec("UPDATE DemoAccount SET balance = balance +" + std::to_string(amount)
        + " WHERE name = \'John\'");

    txn.commit();

    std::cout << ">>>> Transferred " << amount << " between accounts." << std::endl;
} catch (pqxx::sql_error const &e) {
    if (e.sqlstate().compare("40001") == 0) {
        std::cerr << "The operation is aborted due to a concurrent transaction that is modifying the same set of rows."
                    << "Consider adding retry logic for production-grade applications." << std::endl;
    }
    throw e;
}
```

## Learn more

[libpq driver](../../../../reference/drivers/ysql-client-drivers/#libpq)

[libpqxx driver](../../../../reference/drivers/ysql-client-drivers/#libpqxx)
