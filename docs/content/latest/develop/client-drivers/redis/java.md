
## Maven

To build your Java application using the YugaByte Redis driver, add the following Maven dependency to your application:

```
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jedis</artifactId>
  <version>2.9.0-yb-9</version>
</dependency>
```

## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the Redis shell. If not, please follow these steps in the [quick start guide](../../../quick-start/test-redis/).
- installed JDK version 1.8+ and maven 3.3+

### Creating the maven build file

Create a maven build file `pom.xml` and add the following content into it.

```mvn
<?xml version="1.0"?>
<!-- Copyright (c) YugaByte, Inc. -->
<project
  xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd"
  xmlns="http://maven.apache.org/POM/4.0.0"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <modelVersion>4.0.0</modelVersion>

  <groupId>com.yugabyte.sample.apps</groupId>
  <artifactId>hello-world</artifactId>
  <version>1.0</version>
  <packaging>jar</packaging>

  <dependencies>
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>jedis</artifactId>
      <version>2.9.0-yb-9</version>
    </dependency>
  </dependencies>

  <build>
    <plugins>
      <plugin>
        <groupId>org.apache.maven.plugins</groupId>
        <artifactId>maven-dependency-plugin</artifactId>
        <version>2.1</version>
        <executions>
          <execution>
            <id>copy-dependencies</id>
            <phase>prepare-package</phase>
            <goals>
              <goal>copy-dependencies</goal>
            </goals>
            <configuration>
              <outputDirectory>${project.build.directory}/lib</outputDirectory>
              <overWriteReleases>true</overWriteReleases>
              <overWriteSnapshots>true</overWriteSnapshots>
              <overWriteIfNewer>true</overWriteIfNewer>
            </configuration>
          </execution>
        </executions>
      </plugin>
    </plugins>
  </build>
</project>
```

### Writing a HelloWorld Redis app

Create the appropriate directory structure as expected by maven.

```sh
mkdir -p src/main/java/com/yugabyte/sample/apps
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBRedisHelloWorld.java`.

```sh
package com.yugabyte.sample.apps;

import java.util.HashMap;
import java.util.Map;
import redis.clients.jedis.Jedis;

public class YBRedisHelloWorld {
  public static void main(String[] args) {
    try {
      // Create a Jedis client.
      Jedis jedisClient = new Jedis("127.0.0.1");

      // Prepare the employee information to insert.
      String userid = "1";
      Map<String, String> userProfile = new HashMap<String, String>();
      userProfile.put("name", "John");
      userProfile.put("age", "35");
      userProfile.put("language", "Redis");

      // Insert the data.
      String result = jedisClient.hmset(userid, userProfile);
      System.out.println("HMSET returned " + result + ": id=1, name=John, age=35, language=Redis");

      // Query the data.
      Map<String, String> userData = jedisClient.hgetAll(userid);
      System.out.println("Query result: name=" + userData.get("name") +
                         ", age=" + userData.get("age") + ", language=" + userData.get("language"));

      // Close the client.
      jedisClient.close();
    } catch (Exception e) {
        System.err.println("Error: " + e.getMessage());
    }
  }
}
```

### Building and running the app

To build the application, just run the following command.

```sh
mvn package
```

To run the program, do the following.

```sh
java -cp "target/hello-world-1.0.jar:target/lib/*" com.yugabyte.sample.apps.YBRedisHelloWorld
```

You should see the following as the output.

```sh
HMSET returned OK: id=1, name=John, age=35, language=Redis
Query result: name=John, age=35, language=Redis
```
