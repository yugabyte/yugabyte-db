## Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the Redis shell. If not, please follow these steps in the [quick start guide](../../../quick-start/test-redis/).
- installed Visual Studio

## Writing a HelloWorld C# app
In your Visual Studio create a new Project and choose Console Application as template. Follow the instructions to save the project.

### Install StackExchange.Redis C# driver
To install the driver in your Visual Studio project
<ol>
  <li>Open your Project Solution View.</li>
  <li>Right-click on Packages and click Add Packages.</li>
  ![Add Package](/images/develop/client-drivers/csharp/visual-studio-add-package.png)
  <li>Search for StackExchange.Redis and click Add Package.</li>
  ![Search Package](/images/develop/client-drivers/csharp/visual-studio-search-redis-package.png)
</ol>

### Copy the contents below to your `Program.cs` file.

```cs
using System;
using System.Collections.Generic;
using StackExchange.Redis;

namespace YugaByte_CSharp_Demo
{
    class Program
    {
        static private void printHash(HashEntry[] hashes)
        {
            foreach (var hashEntry in hashes)
            {
                Console.WriteLine(string.Format("{0}: {1}", hashEntry.Name, hashEntry.Value));
            }
        }
        static void Main(string[] args)
        {
            try
            {
                ConfigurationOptions config = new ConfigurationOptions
                {
                    EndPoints =
                    {
                        { "127.0.0.1", 6379 },
                    },
                    CommandMap = CommandMap.Create(new HashSet<string>
                    {   // EXCLUDE commands that are not fully supported on YugaByte side.
                        "SUBSCRIBE", "CLUSTER", "TIME", "PING"
                    }, available: false)
                };

                ConnectionMultiplexer connection = ConnectionMultiplexer.Connect(config);
                IDatabase redisDB = connection.GetDatabase();
                var hashKey = "1";
                HashEntry[] setHash = {
                  new HashEntry("name", "John"),
                  new HashEntry("age", 35),
                  new HashEntry("language", "C#"),
                  new HashEntry("client", "Redis")
                };
                Console.WriteLine("Successfully executed HMSET:");
                printHash(setHash);
                redisDB.HashSet(hashKey, setHash);

                var getHash = redisDB.HashGetAll(hashKey);
                Console.WriteLine("Successfully executed HMGET:");
                printHash(getHash);
            }
            catch (RedisConnectionException e)
            {
                Console.WriteLine("Unable to make a connection to local YugaByte DB. " +
                                  "Error:", e.Message);
            }
        }
    }
}
  ```

### Running the C# app
Run the C# app from menu select `Run -> Start Without Debugging`

You should see the following as the output.

```
Successfully executed HMSET:
name: John
age: 35
language: C#
client: Redis
Successfully executed HMGET:
age: 35
client: Redis
language: C#
name: John
```
