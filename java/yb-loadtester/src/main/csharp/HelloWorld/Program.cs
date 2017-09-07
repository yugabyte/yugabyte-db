#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
﻿// Copyright (c) YugaByte, Inc.

using System;
using System.Net;
using System.Collections.Generic;
using Cassandra;

/**
 * Simple C# client which connects to the yugabyte cluster, creates a employee table, inserts
 * sample data into it and reads it back.
 */
namespace YB
{
  class HelloWorld
  {
    public static void Main(string[] args)
    {
      List<IPEndPoint> hostIpAndPorts = new List<IPEndPoint>();
      // We can also add multiple tserver endpoints here. Also replace the local ip
      // with appropriate yugabyte cluster ip.
      hostIpAndPorts.Add(new IPEndPoint(IPAddress.Parse("127.0.0.1"), 9042));

      var cluster = Cluster.Builder()
                           .AddContactPoints(hostIpAndPorts)
                           .Build();

      // Currently our default keyspace name is $$$_DEFAULT, we will create our tables
      // inside of that keyspace.
      var session = cluster.Connect("$$$_DEFAULT");

      // Create a employee table with id as primary key
      session.Execute("CREATE TABLE IF NOT EXISTS employee (id int primary key, name varchar, age int)");

      // Insert some dummy data into the table.

      session.Execute("INSERT INTO employee(id, name, age) values(1, 'John', 35)");

      // Read the data from the table using id.
      var statement = session.Prepare("SELECT * from employee where id = ?").Bind(1);
      RowSet rows = session.Execute(statement);
      Console.WriteLine("Id\tName\tAge");
      foreach (Row row in rows)
        Console.WriteLine("{0}\t{1}\t{2}", row["id"], row["name"], row["age"]);
    }
  }
}
