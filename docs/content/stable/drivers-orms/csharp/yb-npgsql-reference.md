---
title: YugabyteDB Npgsql Smart Driver
headerTitle: C# Drivers
linkTitle: C# Drivers
description: C# Npgsql Smart Driver for YSQL
badges: ysql
menu:
  stable:
    name: C# Drivers
    identifier: ref-1-yb-npgsql-driver
    parent: csharp-drivers
    weight: 100
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

Yugabyte Npgsql smart driver is a .NET driver for [YSQL](../../../api/ysql/) based on [PostgreSQL Npgsql driver](https://github.com/npgsql/npgsql/tree/main/src/Npgsql), with additional connection load balancing features.

For more information on the Yugabyte Npgsql smart driver, see the following:

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [CRUD operations](../ysql/)
- [GitHub repository](https://github.com/yugabyte/npgsql)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)

## Download the driver dependency

If you are using Visual Studio IDE, add the NpgsqlYugabyteDB package to your project as follows:

1. Right-click **Dependencies** and choose **Manage Nuget Packages**
1. Search for `NpgsqlYugabyteDB` and click **Add Package**. You may need to click the **Include prereleases** checkbox.

To add the NpgsqlYugabyteDB package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package NpgsqlYugabyteDB
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/Npgsql/) for NpgsqlYugabyteDB.

## Fundamentals

Learn how to perform common tasks required for C# application development using the Npgsql YugabyteDB driver.

### Load balancing connection properties

The following connection properties need to be added to enable load balancing:

- `Load Balance Hosts` - enable cluster-aware load balancing by setting this property to `true`; disabled by default.
- `Topology Keys` - provide comma-separated geo-location values to enable topology-aware load balancing. Geo-locations can be provided as `cloud.region.zone`. Specify all zones in a region as `cloud.region.*`. To designate fallback locations for when the primary location is unreachable, specify a priority in the form `:n`, where `n` is the order of precedence. For example, `cloud1.datacenter1.rack1:1,cloud1.datacenter1.rack2:2`.

By default, the driver refreshes the list of nodes every 300 seconds (5 minutes). You can change this value by including the `YB Servers Refresh Interval` connection parameter.

### Use the driver

To use the driver, pass new connection properties for load balancing in the connection URL or properties pool.

To enable uniform load balancing across all servers, you set the `Load Balance Hosts` property to `true` in the URL, as per the following example:

```csharp
var connStringBuilder = "Host=127.0.0.1,127.0.0.2,127.0.0.3;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true;"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

You can specify [multiple hosts](../ysql/#use-multiple-addresses) in the connection string in case the primary address fails. After the driver establishes the initial connection, it fetches the list of available servers from the universe, and performs load balancing of subsequent connection requests across these servers.

To specify topology keys, you set the `Topology Keys` property to comma separated values, as per the following example:

```go
var connStringBuilder = "Host=127.0.0.1,127.0.0.2,127.0.0.3;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true;Topology Keys=cloud.region.zone"
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

### Configure SSL/TLS

The YugabyteDB Npgsql smart driver support for SSL is the same as for the upstream driver. For information on using SSL/TLS for your application, refer to the .NET Npgsql driver's [Configure SSL/TLS](../postgres-npgsql-reference/#configure-ssl-tls) instructions.

<!-- The following table describes the additional parameters the YugabyteDB Npgsql smart driver requires as part of the connection string when using SSL.

| YugabyteDB Npgsql Parameter | Description |
| :-------------------------- | :---------- |
| SslMode     | SSL Mode |
| RootCertificate | Path to the root certificate on your computer |
| TrustServerCertificate | For use with the Require SSL mode |

#### SSL modes

YugabyteDB supports SSL modes in different ways depending on the driver version, as shown in the following table.

| SSL mode | Versions before 6.0 | Version 6.0 or later |
| :------- | :------------------ | :------------------- |
| Disable  | Supported (default) | Supported |
| Allow    | Not Supported | Supported |
| Prefer   | Supported | Supported (default) |
| Require  | Supported<br/>For self-signed certificates, set `TrustServerCertificate` to true | Supported<br/>Set `TrustServerCertificate` to true |
| VerifyCA | Not Supported - use Require | Supported |
| VerifyFull | Not Supported - use Require | Supported |

The YugabyteDB Npgsql smart driver validates certificates differently from other PostgreSQL drivers as follows:

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
