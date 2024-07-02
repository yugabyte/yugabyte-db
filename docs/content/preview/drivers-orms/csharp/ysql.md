---
title: C# Npgsql Smart Driver for YSQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a C# application using Npgsql Smart Driver
menu:
  preview:
    identifier: csharp-driver-ysql
    parent: csharp-drivers
    weight: 400
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../postgres-npgsql/" class="nav-link active">
      YSQL
    </a>
  </li>
  <li>
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YugabyteDB Npgsql Smart Driver
    </a>
  </li>

  <li >
    <a href="../postgres-npgsql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL Npgsql Driver
    </a>
  </li>

</ul>

The [Yugabyte Npgsql smart driver](https://github.com/yugabyte/npgsql) is a .NET driver for [YSQL](../../../api/ysql/) built on the [PostgreSQL Npgsql driver](https://github.com/npgsql/npgsql/tree/main/src/Npgsql), with additional [connection load balancing](../../smart-drivers/) features.

{{< note title="YugabyteDB Aeon" >}}

To use smart driver load balancing features when connecting to clusters in YugabyteDB Aeon, applications must be deployed in a VPC that has been peered with the cluster VPC. For applications that access the cluster from outside the VPC network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from outside the VPC network fall back to the upstream driver behaviour automatically. For more information, refer to [Using smart drivers with YugabyteDB Aeon](../../smart-drivers/#using-smart-drivers-with-yugabytedb-aeon).

{{< /note >}}

## CRUD operations

The following sections demonstrate how to perform common tasks required for C# application development using the Yugabyte Npgsql smart driver APIs.

To start building your application, make sure you have met the [prerequisites](../#prerequisites).

### Step 1: Add the Npgsql driver dependency

If you are using Visual Studio, add the Npgsql package to your project as follows:

1. Right-click **Dependencies** and choose **Manage Nuget Packages**.
1. Search for `NpgsqlYugabyteDB` and click **Add Package**. You may need to click the **Include prereleases** checkbox.

To add the Npgsql package to your project when not using an IDE, use the following `dotnet` command:

```csharp
dotnet add package NpgsqlYugabyteDB
```

or any of the other methods mentioned on the [nuget page](https://www.nuget.org/packages/NpgsqlYugabyteDB/) for NpgsqlYugabyteDB.

### Step 2: Set up the database connection

After setting up the dependencies, implement a C# client application that uses the Npgsql YugabyteDB driver to connect to your YugabyteDB cluster and run a query on the sample data.

Import YBNpgsql and use the `NpgsqlConnection` class for getting connection objects for the YugabyteDB database that can be used for performing DDLs and DMLs against the database.

The following table describes the connection parameters required to connect, including [smart driver parameters](../../smart-drivers/) for uniform and topology load balancing.

| Parameter | Description | Default |
| :-------- | :---------- | :------ |
| Host      | Host name of the YugabyteDB instance. You can also enter [multiple addresses](#use-multiple-addresses). | localhost
| Port      |  Listen port for YSQL | 5433
| Database  | Database name | yugabyte
| Username  | User connecting to the database | yugabyte
| Password  | Password for the user | yugabyte
| Load Balance Hosts | [Uniform load balancing](../../smart-drivers/#cluster-aware-connection-load-balancing) | False |
| YB Servers Refresh Interval | If Load Balance Hosts is true, the interval in seconds to refresh the servers list | 300 |
| Topology Keys | [Topology-aware load balancing](../../smart-drivers/#topology-aware-connection-load-balancing) | Null |

{{< note title ="Note" >}}
The behaviour of `Load Balance Hosts` is different in YugabyteDB Npgsql Smart Driver as compared to the upstream driver. The upstream driver balances connections on the list of hosts provided in the `Host` property, whereas the smart driver balances the connections on the list of servers returned by the `yb_servers()` function.
{{< /note >}}

The following is an example of a basic connection string for connecting to YugabyteDB:

```csharp
var connStringBuilder = "Host=localhost;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

After the driver establishes the initial connection, it fetches the list of available servers from the cluster, and load-balances subsequent connection requests across these servers.

#### Use multiple addresses

You can specify multiple hosts in the connection string to provide alternative options during the initial connection in case the primary address fails. Delimit the addresses using commas, as follows:

```csharp
var connStringBuilder = "Host=127.0.0.1,127.0.0.2,127.0.0.3;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

#### Use topology-aware load balancing

To use topology-aware load balancing, specify the topology keys by setting the `Topology Keys` parameter, as per the following example:

```csharp
var connStringBuilder = "Host=127.0.0.1,127.0.0.2,127.0.0.3;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true;Topology Keys=cloud.region.zone"
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

You can pass multiple keys to the `Topology Keys` property, and give each of them a preference value, as per the following example:

```csharp
var connStringBuilder = "Host=127.0.0.1,127.0.0.2,127.0.0.3;Port=5433;Database=yugabyte;Username=yugabyte;Password=password;Load Balance Hosts=true;Topology Keys=cloud1.region1.zone1:1,cloud2.region2.zone2:2";
NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder)
```

#### Use SSL

The YugabyteDB Npgsql smart driver support for SSL is the same as for the upstream driver. To set up the driver properties to configure the credentials and SSL certificates for connecting to your cluster, refer to [Use SSL](../postgres-npgsql/#use-ssl).

### Step 3 : Write your application

Copy the following code to the `Program.cs` file to set up YugbyteDB tables and query the table contents from the C# client. Replace the connection string `connStringBuilder` with the credentials of your cluster, and SSL certificates if required.

```csharp
using System;
using YBNpgsql;
namespace Yugabyte_CSharp_Demo
{
   class Program
   {
       static void Main(string[] args)
       {
           var connStringBuilder = "host=localhost;port=5433;database=yugabyte;userid=yugabyte;password=xxx;Load Balance Hosts=true";
           NpgsqlConnection conn = new NpgsqlConnection(connStringBuilder);
           try
           {
               conn.Open();
               NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
               empCreateCmd.ExecuteNonQuery();
               Console.WriteLine("Created table Employee");
               NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
               int numRows = empInsertCmd.ExecuteNonQuery();
               Console.WriteLine("Inserted data (1, 'John', 35, 'CSharp')");
               NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
               empPrepCmd.Parameters.Add("@EmployeeId", YBNpgsqlTypes.NpgsqlDbType.Integer);
               empPrepCmd.Parameters["@EmployeeId"].Value = 1;
               NpgsqlDataReader reader = empPrepCmd.ExecuteReader();
               Console.WriteLine("Query returned:\nName\tAge\tLanguage");
               while (reader.Read())
               {
                   Console.WriteLine("{0}\t{1}\t{2}", reader.GetString(0), reader.GetInt32(1), reader.GetString(2));
               }
           }
           catch (Exception ex)
           {
               Console.WriteLine("Failure: " + ex.Message);
           }
           finally
           {
               if (conn.State != System.Data.ConnectionState.Closed)
               {
                   conn.Close();
               }
           }
       }
   }
}
```

## Run the application

To run the project `Program.cs` in Visual Studio Code, from the **Run** menu, choose **Start Without Debugging**. If you aren't using an IDE, enter the following command:

```csharp
dotnet run
```

You should see output similar to the following:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp')
Query returned:
Name  Age  Language
John  35   CSharp
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
- [Smart Driver architecture](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/smart-driver.md)
- [YugabyteDB Npgsql Smart Driver reference](../../../reference/drivers/csharp/yb-npgsql-reference/)
- Build C# applications using [Entity Framework ORM](../entityframework)
