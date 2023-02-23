---
title: C# Drivers
linkTitle: C# Drivers
description: C# Drivers for YSQL
headcontent: C# Drivers for YSQL
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: C# Drivers
    identifier: ref-yb-npgsql-driver
    parent: drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../yb-npgsql-reference/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB Npgsql Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-npgsql-reference/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Npgsql Driver
    </a>
  </li>

</ul>

The [Yugabyte Npgsql smart driver](https://github.com/yugabyte/npgsql) is a distributed .NET driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL Npgsql driver](https://github.com/npgsql/npgsql/tree/main/src/Npgsql), with additional [connection load balancing](../../smart-drivers/) features.

## Fundamentals

Learn how to perform common tasks required for C# application development using the NpgsqlYugabyteDB driver.

### Download the driver dependency

If you are using Visual Studio IDE, add the NpgsqlYugabyteDB package to your project as follows:

1. Right-click **Dependencies** and choose **Manage Nuget Packages**
1. Search for `NpgsqlYugabyteDB` and click **Add Package**. You may need to click the **Include prereleases** checkbox.

To add the NpgsqlYugabyteDB package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package NpgsqlYugabyteDB
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for NpgsqlYugabyteDB.

### Connect to YugabyteDB database

After setting up the dependencies, implement the C# client application that uses the NpgsqlYugabyteDB driver to connect to your YugabyteDB cluster and run a query on the sample data.

Import NpgsqlYugabyteDB and use the `NpgsqlConnection` class to create the connection object to perform DDLs and DMLs against the database.

The following table describes the connection parameters for connecting to the YugabyteDB database.

| Parameters | Description | Default |
| :---------- | :---------- | :------ |
| Host  | Host name of the YugabyteDB instance | localhost
| Port |  Listen port for YSQL | 5433
| Database | Database name | yugabyte
| Username | User connecting to the database | yugabyte
| Password | Password for the user | yugabyte
| Load Balance Hosts | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | False |
| Topology Keys | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | Null |
| YB Servers Refresh Interval | Parameter to regulate the refresh interval | 300 seconds |

{{< note title ="Note" >}}
The behaviour of `Load Balance Hosts` is different in YugabyteDB Npgsql Driver as compared to the upstream driver. The upstream driver balances connections on the list of hosts provided in the `Host` property, whereas the YugabyteDB Npgsql Driver balances the connections on the list of servers returned by the `yb_servers()` function.
{{< /note >}}

The following is a basic example connection string for connecting to YugabyteDB.

```csharp
var connStringBuilder = "Host=localhost;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
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
empPrepCmd.Parameters.Add("@EmployeeId", YBNpgsqlTypes.NpgsqlDbType.Integer);

empPrepCmd.Parameters["@EmployeeId"].Value = 1;
NpgsqlDataReader reader = empPrepCmd.ExecuteReader();

Console.WriteLine("Query returned:\nName\tAge\tLanguage");
while (reader.Read())
{
    Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
}
```

<!-- ### Configure SSL/TLS

The following table describes the additional parameters the .NET Npgsql driver requires as part of the connection string when using SSL.

| Npgsql Parameter | Description |
| :---------- | :---------- |
| SslMode     | SSL Mode |
| RootCertificate | Path to the root certificate on your computer |
| TrustServerCertificate | For use with the Require SSL mode |

Npgsql supports SSL modes in different ways depending on the driver version, as shown in the following table.

| SSL mode | Versions before 6.0 | Version 6.0 or later |
| :------- | :------------------------ | :----|
| Disable  | Supported (default) | Supported |
| Allow    | Not Supported | Supported |
| Prefer   | Supported | Supported (default) |
| Require  | Supported<br/>For self-signed certificates, set `TrustServerCertificate` to true | Supported<br/>Set `TrustServerCertificate` to true |
| VerifyCA | Not Supported - use Require | Supported |
| VerifyFull | Not Supported - use Require | Supported |

The .NET Npgsql driver validates certificates differently from other PostgreSQL drivers as follows:

- Prior to version 6.0, when you specify SSL mode `Require`, you also need to specify `RootCertificate`, and the driver verifies the certificate by default (like the verify CA or verify full modes on other drivers), and fails for self-signed certificates.

  To use self-signed certificates, specify `TrustServerCertificate=true`, which bypasses walking the certificate chain to validate trust and hence works like other drivers' require mode. In this case, you don't need to specify the `RootCertificate`.

- For version 6.0 and later, the `Require` SSL mode requires explicitly setting the `TrustServerCertificate` field to true.

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
    connStringBuilder.RootCertificate = "/root.crt"; //Provide full path to your root CA.
    connStringBuilder.Username = "admin";
    connStringBuilder.Password = "xxxxxx";
    connStringBuilder.Database = "yugabyte";
    CRUD(connStringBuilder.ConnectionString);
```

For more information on TLS/SSL support, see [Security and Encryption](https://www.npgsql.org/doc/security.html?tabs=tabid-1) in the Npgsql documentation. -->

## Compatibility matrix

| Driver Version | YugabyteDB Version | Support |
| :------------- | :----------------- | :------ |
| 6.0.3 | 2.17 (preview) | full
| 6.0.3 | 2.16 (stable) | full
