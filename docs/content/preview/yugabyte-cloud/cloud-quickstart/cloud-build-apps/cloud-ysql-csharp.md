---
title: Build a C# application that uses YSQL
headerTitle: Build a C# application
description: Build a small C# application using the Npgsql driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: Npgsql"
menu:
  preview_yugabyte-cloud:
    parent: cloud-build-apps
    name: C#
    identifier: cloud-csharp
    weight: 600
type: docs
---

The following tutorial shows a small [C# application](https://github.com/yugabyte/yugabyte-simple-csharp-app) that connects to a YugabyteDB cluster using the [Npgsql driver](../../../../reference/drivers/ysql-client-drivers/#npgsql) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C#.

## Prerequisites

- [.NET 6.0 SDK](https://dotnet.microsoft.com/en-us/download) or later.

### YugabyteDB Managed

- You have a cluster deployed in YugabyteDB Managed. To get started, use the [Quick start](../../).
- You downloaded the cluster CA certificate and added your computer to the cluster IP allow list. Refer to [Before you begin](../cloud-add-ip/).

## Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-csharp-app.git && cd yugabyte-simple-csharp-app
```

The `yugabyte-simple-csharp-app.csproj` file includes the following package reference to include the driver:

```cpp
<PackageReference Include="npgsql" Version="6.0.3" />
```

## Provide connection parameters

The application needs to establish a connection to the YugabyteDB cluster. To do this:

1. Open the `sample-app.cs` file.

2. Set the following configuration-related parameters:

    - **urlBuilder.Host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **urlBuilder.Port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
    - **urlBuilder.Database** - the name of the database you are connecting to (the default database is named `yugabyte`).
    - **urlBuilder.Username** and **urlBuilder.Password** - the username and password for the YugabyteDB database. If you are using the credentials you created when deploying a cluster in YugabyteDB Managed, these can be found in the credentials file you downloaded.
    - **urlBuilder.SslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql); use `SslMode.VerifyFull`.
    - **urlBuilder.RootCertificate** - the full path to the YugabyteDB Managed cluster CA certificate.

3. Save the file.

## Build and run the application

Build and run the application.

```sh
dotnet run
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

You have successfully executed a basic C# application that works with YugabyteDB Managed.

## Explore the application logic

Open the `sample-app.cs` file in the `yugabyte-simple-csharp-app` folder to review the methods.

### connect

The `connect` method establishes a connection with your cluster via the Npgsql driver. To avoid making extra system table queries to map types, the `ServerCompatibilityMode` is set to `NoTypeLoading`.

```cpp
NpgsqlConnectionStringBuilder urlBuilder = new NpgsqlConnectionStringBuilder();
urlBuilder.Host = "";
urlBuilder.Port = 5433;
urlBuilder.Database = "yugabyte";
urlBuilder.Username = "";
urlBuilder.Password = "";
urlBuilder.SslMode = SslMode.VerifyFull;
urlBuilder.RootCertificate = "";

urlBuilder.ServerCompatibilityMode = ServerCompatibilityMode.NoTypeLoading;

NpgsqlConnection conn = new NpgsqlConnection(urlBuilder.ConnectionString);

conn.Open();
```

### createDatabase

The `createDatabase` method uses PostgreSQL-compliant DDL commands to create a sample database.

```cpp
NpgsqlCommand query = new NpgsqlCommand("DROP TABLE IF EXISTS DemoAccount", conn);
query.ExecuteNonQuery();

query = new NpgsqlCommand("CREATE TABLE DemoAccount (" +
            "id int PRIMARY KEY," +
            "name varchar," +
            "age int," +
            "country varchar," +
            "balance int)", conn);
query.ExecuteNonQuery();

query = new NpgsqlCommand("INSERT INTO DemoAccount VALUES" +
            "(1, 'Jessica', 28, 'USA', 10000)," +
            "(2, 'John', 28, 'Canada', 9000)", conn);
query.ExecuteNonQuery();
```

### selectAccounts

The `selectAccounts` method queries your distributed data using the SQL `SELECT` statement.

```cpp
NpgsqlCommand query = new NpgsqlCommand("SELECT name, age, country, balance FROM DemoAccount", conn);

NpgsqlDataReader reader = query.ExecuteReader();

while (reader.Read())
{
    Console.WriteLine("name = {0}, age = {1}, country = {2}, balance = {3}",
        reader.GetString(0), reader.GetInt32(1), reader.GetString(2), reader.GetInt32(3));
}
```

### transferMoneyBetweenAccounts

The `transferMoneyBetweenAccounts` method updates your data consistently with distributed transactions.

```cpp
try
{
    NpgsqlTransaction tx = conn.BeginTransaction();

    NpgsqlCommand query = new NpgsqlCommand("UPDATE DemoAccount SET balance = balance - " +
        amount + " WHERE name = \'Jessica\'", conn, tx);
    query.ExecuteNonQuery();

    query = new NpgsqlCommand("UPDATE DemoAccount SET balance = balance + " +
        amount + " WHERE name = \'John\'", conn, tx);
    query.ExecuteNonQuery();

    tx.Commit();

    Console.WriteLine(">>>> Transferred " + amount + " between accounts");

} catch (NpgsqlException ex)
{
    if (ex.SqlState != null && ex.SqlState.Equals("40001"))
    {
        Console.WriteLine("The operation is aborted due to a concurrent transaction that is modifying the same set of rows." +
                "Consider adding retry logic for production-grade applications.");
    }

    throw ex;
}
```

## Learn more

[Npgsql driver](../../../../reference/drivers/ysql-client-drivers/#npgsql)

[Explore more applications](../../../cloud-examples/)

[Deploy clusters in YugabyteDB Managed](../../../cloud-basics)

[Connect to applications in YugabyteDB Managed](../../../cloud-connect/connect-applications/)
