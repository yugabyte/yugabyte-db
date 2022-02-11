---
title: Entity Framework
linkTitle: Entity Framework
description: Entity Framework
section: INTEGRATIONS
menu:
  latest:
    identifier: entity-framework
    weight: 660
isTocNested: true
showAsideToc: true
---

This document describes how to use [Entity Framework Core](https://docs.microsoft.com/en-us/ef/core/), an ORM library for C# with YugabyteDB.

## Prerequisites

Before you start using Entity framework:

- Install YugabyteDB 2.6 or later and start a single node local cluster. Refer [YugabyteDB Quick Start Guide](/latest/quick-start/) to install and start a local cluster.

- Install [.NET Core SDK](https://dotnet.microsoft.com/en-us/download) 6.0 or later.

## Use Entity Framework

You can start using Entity Framework with YugabyteDB as follows:

- Create a new dotnet project with the command:

```csharp
dotnet new console -o EFGetStarted && cd EFGetStarted
```

- Install Entity Framework Core by installing the package for the database provider(s) you want to target. In this case, it is [Entity Framework PostgreSQL](https://www.nuget.org/packages/Npgsql.EntityFrameworkCore.PostgreSQL).

```csharp
dotnet add package Npgsql.EntityFrameworkCore.PostgreSQL
```

- [Create a model](https://docs.microsoft.com/en-us/ef/core/modeling/) from the existing database in a `Model.cs` file using an editor and add the following code:

```cs
using System.Collections.Generic;
using Microsoft.EntityFrameworkCore;

namespace ConsoleApp.PostgreSQL
{
    public class BloggingContext : DbContext
    {
        public DbSet<Blog> Blogs { get; set; }
        public DbSet<Post> Posts { get; set; }

        protected override void OnConfiguring(DbContextOptionsBuilder optionsBuilder)
            => optionsBuilder.UseNpgsql("Host=localhost;Port=5433;Database=yugabyte;Username=yugabyte;Password=yugabyte");
    }

    public class Blog
    {
        public int BlogId { get; set; }
        public string Url { get; set; }
        public List<Post> Posts { get; set; }
    }

    public class Post
    {
        public int PostId { get; set; }
        public string Title { get; set; }
        public string Content { get; set; }

        public int BlogId { get; set; }
        public Blog Blog { get; set; }
    }
}
```

Configure the database properties accordingly. The default `user` and `password` for YugabyteDB is `yugabyte`, and the default `port` is `5433`.

- Use [EF Migrations](https://docs.microsoft.com/en-us/ef/core/managing-schemas/migrations/?tabs=dotnet-core-cli) to create a database from the model.

```csharp
dotnet tool install --global dotnet-ef
dotnet add package Microsoft.EntityFrameworkCore.Design --version 6.0.0 //Mention the version compatible with your .NET SDK version
dotnet ef migrations add InitialCreate
dotnet ef database update
```

- Perform the CRUD operations after the database is created. Replace the code in `Program.cs` file under the `EFGetStarted` project with the following:

```cs
using System;
using System.Linq;

namespace ConsoleApp.PostgreSQL
{
    internal class Program
    {
        private static void Main()
        {
            using (var db = new BloggingContext())
            {
                // Note: This sample requires the database to be created before running.
                // Console.WriteLine($"Database path: {db.DbPath}.");

                // Create
                Console.WriteLine("Inserting a new blog");
                db.Add(new Blog { Url = "http://blogs.abc.com/adonet" });
                db.SaveChanges();

                // Read
                Console.WriteLine("Querying for a blog");
                var blog = db.Blogs
                    .OrderBy(b => b.BlogId)
                    .First();
                Console.WriteLine("ID :" + blog.BlogId + "\nURL:" + blog.Url);

                // Delete
                Console.WriteLine("Deleting the blog");
                db.Remove(blog);
                db.SaveChanges();
            }
        }
    }
}
```

- Run the application and verify the results.

```csharp
dotnet run
```

```output
Inserting a new blog
Querying for a blog
ID :1
URL:http://blogs.abc.com/adonet
Deleting the blog
```
