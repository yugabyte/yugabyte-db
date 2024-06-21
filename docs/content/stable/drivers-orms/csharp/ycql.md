---
title: Connect an application using YugabyteDB C# driver for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect a C# application using YugabyteDB YCQL driver
menu:
  stable:
    identifier: csharp-driver-ycql
    parent: csharp-drivers
    weight: 500
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql/" class="nav-link">
      YSQL
    </a>
  </li>
  <li class="active">
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB C# Driver
    </a>
  </li>

</ul>

[Yugabyte C# Driver for YCQL](https://github.com/yugabyte/cassandra-csharp-driver) is based on [DataStax C# Driver](https://github.com/datastax/csharp-driver) with additional [smart driver](../../smart-drivers-ycql/) features.

{{< note title="YugabyteDB Aeon" >}}

To use the driver's partition-aware load balancing feature in a YugabyteDB Aeon cluster, applications must be deployed in a VPC that has been peered with the cluster VPC so that they have access to all nodes in the cluster. For more information, refer to [Using YCQL drivers with YugabyteDB Aeon](../../smart-drivers-ycql/#using-ycql-drivers-with-yugabytedb-aeon).

{{< /note >}}

## Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe and are able to interact with it using the YCQL shell. If not, follow the steps in [Quick start](/preview/quick-start/).
- installed Visual Studio.

## Write the HelloWorld C# app

In your Visual Studio create a new **Project** and choose **Console Application** as template. Follow the instructions to save the project.

### Install YugabyteDB C# Driver for YCQL

The [Yugabyte C# Driver for YCQL](https://github.com/yugabyte/cassandra-csharp-driver) is based on a fork of the Apache Cassandra C# Driver, but adds features unique to YCQL, including [JSONB support](../../../api/ycql/type_jsonb/) and a different routing policy.

To install the [Yugabyte C# Driver for YCQL](https://www.nuget.org/packages/YugaByteCassandraCSharpDriver/) in your Visual Studio project, follow the instructions in the [README](https://github.com/yugabyte/cassandra-csharp-driver).

### Create the program

Copy the contents below to your `Program.cs` file:

```csharp
using System;
using System.Linq;
using Cassandra;

namespace Yugabyte_CSharp_Demo
{
    class Program
    {
        static void Main(string[] args)
        {
            try
            {
                var cluster = Cluster.Builder()
                                     .AddContactPoints("127.0.0.1")
                                     .WithPort(9042)
                                     .Build();
                var session = cluster.Connect();
                session.Execute("CREATE KEYSPACE IF NOT EXISTS ybdemo");
                Console.WriteLine("Created keyspace ybdemo");

                var createStmt = "CREATE TABLE IF NOT EXISTS ybdemo.employee(" +
                    "id int PRIMARY KEY, name varchar, age int, language varchar)";
                session.Execute(createStmt);
                Console.WriteLine("Created keyspace employee");

                var insertStmt = "INSERT INTO ybdemo.employee(id, name, age, language) " +
                    "VALUES (1, 'John', 35, 'C#')";
                session.Execute(insertStmt);
                Console.WriteLine("Inserted data: {0}", insertStmt);

                var preparedStmt = session.Prepare("SELECT name, age, language " +
                                                   "FROM ybdemo.employee WHERE id = ?");
                var selectStmt = preparedStmt.Bind(1);
                var result = session.Execute(selectStmt);
                var rows = result.GetRows().ToList();
                Console.WriteLine("Select query returned {0} rows", rows.Count());
                Console.WriteLine("Name\tAge\tLanguage");
                foreach (Row row in rows)
                    Console.WriteLine("{0}\t{1}\t{2}", row["name"], row["age"], row["language"]);

                session.Dispose();
                cluster.Dispose();

            }
            catch (Cassandra.NoHostAvailableException)
            {
                Console.WriteLine("Make sure YugabyteDB is running locally!.");
            }
            catch (Cassandra.InvalidQueryException ie)
            {
                Console.WriteLine("Invalid Query: " + ie.Message);
            }
        }
    }
}
```

### Run the application

To run the C# app from the Visual Studio menu, select `Run > Start Without Debugging`.

You should see the following as the output.

```output
Created keyspace ybdemo
Created keyspace employee
Inserted data: INSERT INTO ybdemo.employee(id, name, age, language) VALUES (1, 'John', 35, 'C#')
Select query returned 1 rows
Name  Age  Language
John  35   C#
```
