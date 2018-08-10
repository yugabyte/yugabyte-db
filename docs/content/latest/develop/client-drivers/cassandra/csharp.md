## Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the CQL shell. If not, please follow these steps in the [quick start guide](/quick-start/test-cassandra/).
- installed Visual Studio

## Writing a HelloWorld C# app
In your Visual Studio create a new Project and choose Console Application as template. Follow the instructions to save the project.

### Install Cassandra C# driver
To install the driver in your Visual Studio project
<ol>
  <li>Open your Project Solution View.</li>
  <li>Right-click on Packages and click Add Packages.</li>
  ![Add Package](/images/develop/client-drivers/csharp/visual-studio-add-package.png)
  <li>Search for CassandraCSharpDriver and click Add Package.</li>
  ![Search Package](/images/develop/client-drivers/csharp/visual-studio-search-cassandra-package.png)
</ol>


### Copy the contents below to your `Program.cs` file.

```{.cs .copy}
using System;
using System.Linq;
using Cassandra;

namespace YugaByte_CSharp_Demo
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
                Console.WriteLine("Make sure YugaByteDB is running locally!.");
            }
            catch (Cassandra.InvalidQueryException ie)
            {
                Console.WriteLine("Invalid Query: " + ie.Message);
            }
        }
    }
}
```

### Running the C# app
Run the C# app from menu select `Run -> Start Without Debugging`

You should see the following as the output.

```
Created keyspace ybdemo
Created keyspace employee
Inserted data: INSERT INTO ybdemo.employee(id, name, age, language) VALUES (1, 'John', 35, 'C#')
Select query returned 1 rows
Name	Age	Language
John	35	C#
```
