---
title: Build a C# application that uses YSQL
headerTitle: Build a C# application
linkTitle: C#
description: Use C# to build a YugabyteDB application that uses YSQL
menu:
  stable:
    identifier: build-apps-csharp-1-ysql
    parent: build-apps
    weight: 554
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li>
    <a href="{{< relref "./ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

{{< tip title="Yugabyte Cloud requires SSL" >}}

Are you using Yugabyte Cloud? Install the [prerequisites](#prerequisites), then go to the [Use C# with SSL](#use-c-with-ssl) section.

{{</ tip >}}

## Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe, and are able to interact with it using the YSQL shell (`ysqlsh`). If not, follow the steps in [Quick start](../../../../quick-start).
- installed Visual Studio.

{{< warning title="Warning" >}}

On every new connection the NpgSQL driver also makes [extra system table queries to map types](https://github.com/npgsql/npgsql/issues/1486), which adds significant overhead. To turn off this behavior, set the following option in your connection string builder:

```csharp
connStringBuilder.ServerCompatibilityMode = ServerCompatibilityMode.NoTypeLoading;
```

{{< /warning >}}

## Create a sample C# application

In Visual Studio, create a new project and choose **Console Application as template**. Follow the instructions to save the project.

First, install the Npgsql driver in your Visual Studio project:

1. Open your Project Solution View
1. Right-click on **Packages** and click **Add Packages**
1. Search for `Npgsql` and click **Add Package**

Next, copy the contents below to your `Program.cs` file:

```csharp
using System;
using Npgsql;

namespace Yugabyte_CSharp_Demo
{
    class Program
    {
        static void Main(string[] args)
        {
            NpgsqlConnection conn = new NpgsqlConnection("host=localhost;port=5433;database=yb_demo;user id=yugabyte;password=");

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
                empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);

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

### Run the C# application

Run the C# app. Select `Run -> Start Without Debugging`.

You should see the following as the output:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp')
Query returned:
Name  Age  Language
John  35   CSharp
```

## Use C# with SSL

The client driver supports several SSL modes, as follows:

| SSL mode | Client driver behavior |
| :------- | :--------------------- |
| disable | Supported |
| allow | Not Supported |
| prefer | Supported |
| require | Supported  |
| verify-ca | Supported <br/> (Self-signed certificates aren't supported.) |
| verify-full | Supported <br/> (Self-signed certificates aren't supported.) |

{{< note title="SSL mode support" >}}

The Npgsql driver does not support the strings `verify-ca` and `verify-full` when specifying the SSL mode.

The .NET Npgsql driver validates certificates differently from other PostgreSQL drivers. When you specify SSL mode `require`, the driver verifies the certificate by default (like the `verify-ca` or `verify-full` modes), and fails for self-signed certificates. You can override this by specifying "Trust Server Certificate=true", in which case it bypasses walking the certificate chain to validate trust and hence works like other drivers' `require` mode. In this case, the Root-CA certificate is not required to be configured.

{{< /note >}}

### Create a sample C# application with SSL

In Visual Studio, create a new project and choose **Console Application as template**. Follow the instructions to save the project.

First, install the Npgsql driver in your Visual Studio project, replacing the values in the `connStringBuilder` object as appropriate for your cluster::

1. Open your Project Solution View
1. Right-click on **Packages** and click **Add Packages**
1. Search for `Npgsql` and click **Add Package**

Next, copy the contents below to your `Program.cs` file, :

```csharp
using System;
using Npgsql;
 
namespace Yugabyte_CSharp_Demo
{
   class Program
   {
       static void Main(string[] args)
       {
          var connStringBuilder = new NpgsqlConnectionStringBuilder();
           connStringBuilder.Host = "22420e3a-768b-43da-8dcb-xxxxxx.aws.ybdb.io";
           connStringBuilder.Port = 5433;
           connStringBuilder.SslMode = SslMode.Require;
           connStringBuilder.Username = "admin";
           connStringBuilder.Password = "xxxxxx";
           connStringBuilder.Database = "yugabyte";
           connStringBuilder.TrustServerCertificate = true;
           CRUD(connStringBuilder.ConnectionString);
       }
       static void CRUD(string connString)
       {
            NpgsqlConnection conn = new NpgsqlConnection(connString);
           try
           {
               conn.Open();
 
               NpgsqlCommand empDropCmd = new NpgsqlCommand("DROP TABLE if exists employee;", conn);
               empDropCmd.ExecuteNonQuery();
               Console.WriteLine("Dropped table Employee");
 
               NpgsqlCommand empCreateCmd = new NpgsqlCommand("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, language varchar);", conn);
               empCreateCmd.ExecuteNonQuery();
               Console.WriteLine("Created table Employee");
 
               NpgsqlCommand empInsertCmd = new NpgsqlCommand("INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'CSharp');", conn);
               int numRows = empInsertCmd.ExecuteNonQuery();
               Console.WriteLine("Inserted data (1, 'John', 35, 'CSharp + SSL')");
 
               NpgsqlCommand empPrepCmd = new NpgsqlCommand("SELECT name, age, language FROM employee WHERE id = @EmployeeId", conn);
               empPrepCmd.Parameters.Add("@EmployeeId", NpgsqlTypes.NpgsqlDbType.Integer);
 
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

### Run the C# SSL application

Run the C# app. Select `Run -> Start Without Debugging`.

You should see the following as the output:

```output
Created table Employee
Inserted data (1, 'John', 35, 'CSharp + SSL')
Query returned:
Name  Age  Language
John  35   CSharp + SSL
```
