---
title: Build a C# application using the Npgsql driver
headerTitle: Build a C# application
description: Build a small C# application using the Npgsql driver and using the YSQL API to connect to and interact with a YugabyteDB Managed cluster.
headContent: "Client driver: Npgsql"
menu:
  stable:
    parent: build-apps
    name: C#
    identifier: cloud-csharp
    weight: 600
type: docs
---

The following tutorial shows a small [C# application](https://github.com/yugabyte/yugabyte-simple-csharp-app) that connects to a YugabyteDB cluster using the [Npgsql driver](../../../../reference/drivers/ysql-client-drivers/#npgsql) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in C#.

## Prerequisites

[.NET 6.0 SDK](https://dotnet.microsoft.com/en-us/download) or later.

### Clone the application from GitHub

Clone the sample application to your computer:

```sh
git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-csharp-app.git && cd yugabyte-simple-csharp-app
```

The `yugabyte-simple-csharp-app.csproj` file includes the following package reference to include the driver:

```cpp
<PackageReference Include="npgsql" Version="6.0.3" />
```

## Provide connection parameters

If your cluster is running on YugabyteDB Managed, you need to modify the connection parameters so that the application can establish a connection to the YugabyteDB cluster. (You can skip this step if your cluster is running locally and listening on 127.0.0.1:5433.)

To do this:

1. Open the `sample-app.cs` file.

2. Set the following configuration-related parameters:

    - **urlBuilder.Host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
    - **urlBuilder.Port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
    - **urlBuilder.Database** - the name of the database you are connecting to (the default is `yugabyte`).
    - **urlBuilder.Username** and **urlBuilder.Password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte` and `yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.
    - **urlBuilder.SslMode** - the SSL mode to use. YugabyteDB Managed [requires SSL connections](../../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/); use `SslMode.VerifyFull`.
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
