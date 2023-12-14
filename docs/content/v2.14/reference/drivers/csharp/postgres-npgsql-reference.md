---
title: C# Drivers
linkTitle: C# Drivers
description: C# Drivers for YSQL
headcontent: C# Drivers for YSQL
menu:
  v2.14:
    name: C# Drivers
    identifier: ref-postgres-npgsql-driver
    parent: drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../postgres-npgsql-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Npgsql
    </a>
  </li>

</ul>

Npgsql is an open source ADO.NET Data Provider for PostgreSQL; it allows programs written in C#, Visual Basic, and F# to access the YugabyteDB server.

## Quick start

Learn how to establish a connection to YugabyteDB database and begin CRUD operations using the steps from [Build a C# application](../../../../quick-start/build-apps/csharp/ysql).

## Download the driver dependency

If you are using Visual Studio IDE, add the Npgsql package to your project as follows:

1. Right-click **Dependencies** and choose **Manage Nuget Packages**
1. Search for `Npgsql` and click **Add Package**

To add the Npgsql package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package Npgsql
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for Npgsql.

## Fundamentals

Learn how to perform common tasks required for C# application development using the Npgsql driver.

### Connect to YugabyteDB database

After setting up the dependencies, implement the C# client application that uses the Npgsql driver to connect to your YugabyteDB cluster and run a query on the sample data.

Import Npgsql and use the `NpgsqlConnection` class to create the connection object to perform DDLs and DMLs against the database.

The following table describes the connection parameters required to connect to the YugabyteDB database.

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| host  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | Database name | yugabyte
| user | User connecting to the database | yugabyte
| password | Password for the user | yugabyte

The following is an example connection string for connecting to YugabyteDB.

```csharp
var connStringBuilder = "host=localhost;port=5433;database=yugabyte;user id=yugabyte;password="
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder);
```
### Create table

Tables can be created in YugabyteDB by passing the `CREATE TABLE` DDL statement to the `NpgsqlCommand` class and getting a command object, then calling the `ExecuteNonQuery()` method using this command object.

```sql
CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar)
```

```csharp
conn.Open();
NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
empCreateCmd.ExecuteNonQuery();
```

### Read and write data

#### Insert data

To write data into YugabyteDB, execute the `INSERT` statement using the `NpgsqlCommand` class, get a command object, and then call the `ExecuteNonQuery()` method using this command object.

```sql
INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');
```

```csharp
NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
int numRows = empInsertCmd.ExecuteNonQuery();
```

#### Query data

To query data from YugabyteDB tables, execute the `SELECT` statement using the `NpgsqlCommand` class, get a command object, and then call the `ExecuteReader()` function using the object. Loop through the reader to get the list of returned rows.

```sql
SELECT * from employee where id=1;
```

```csharp
NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);

empPrepCmd.Parameters["@EmployeeId"].Value = 1;
NpgsqlDataReader reader = empPrepCmd.ExecuteReader();

Console.WriteLine("Query returned:\nName\tAge\tLanguage");
while (reader.Read())
{
    Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
}
```

### Configure SSL/TLS

For Npgsql versions before 6.0, the client driver supports the following SSL modes:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Not Supported |
| prefer | Supported |
| require | Supported <br/> (Self-signed certificates aren't supported.) |
| verify-ca | Not Supported  |
| verify-full | Not Supported |

The .NET Npgsql driver validates certificates differently from other PostgreSQL drivers. When you specify SSL mode `require`, the driver verifies the certificate by default (like the `verify-ca` or `verify-full` modes), and fails for self-signed certificates. You can override this by specifying "Trust Server Certificate=true", in which case it bypasses walking the certificate chain to validate trust and hence works like other drivers' `require` mode. In this case, the Root-CA certificate is not required to be configured.

For versions 6.0 or later, the client driver supports the following SSL modes:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Supported |
| prefer(default) | Supported |
| require | Supported  |
| verify-ca | Supported  |
| verify-full | Supported |

The `Require` SSL mode currently requires explicitly setting the `TrustServerCertificate=true` field.

The following example shows how to build a connection string for connecting to a YugabyteDB cluster using the `Require` SSL mode.

```csharp
var connStringBuilder = new NpgsqlConnectionStringBuilder();
    connStringBuilder.Host = "22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io";
    connStringBuilder.Port = 5433;
    connStringBuilder.SslMode = SslMode.Require;
    connStringBuilder.Username = "admin";
    connStringBuilder.Password = "xxxxxx";
    connStringBuilder.Database = "yugabyte";
    connStringBuilder.TrustServerCertificate = true;
    CRUD(connStringBuilder.ConnectionString);
```

The following example shows how to build a connection string for connecting to a YugabyteDB cluster using the `VerifyCA` and `VerifyFull` SSL modes.

```csharp
var connStringBuilder = new NpgsqlConnectionStringBuilder();
    connStringBuilder.Host = "22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io";
    connStringBuilder.Port = 5433;
    connStringBuilder.SslMode = SslMode.VerifyCA;
    //or connStringBuilder.SslMode = SslMode.VerifyFull;
    connStringBuilder.RootCertificate = "/root.crt";
    connStringBuilder.Username = "admin";
    connStringBuilder.Password = "xxxxxx";
    connStringBuilder.Database = "yugabyte";
    CRUD(connStringBuilder.ConnectionString);
```

## Compatibility matrix

| Driver Version | YugabyteDB Version | Support |
| :------------- | :----------------- | :------ |
| 6.0.3 | 2.11 (preview) | full
| 6.0.3 |  2.8 (stable) | full
| 6.0.3 | 2.6 | full

## Other usage examples

[Sample C# application with SSL](/preview/quick-start/build-apps/csharp/ysql/#create-a-sample-c-application-with-ssl)
