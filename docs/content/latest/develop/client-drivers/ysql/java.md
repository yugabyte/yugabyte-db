
## Maven

To build your Java application using the Postgresql JDBC driver, add the following Maven dependency to your application:

```mvn
<dependency>
  <groupId>org.postgresql</groupId>
  <artifactId>postgresql</artifactId>
  <version>42.2.5</version>
</dependency>
```


## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB and created a universe with YSQL enabled. If not, please follow these steps in the [quick start guide](../../../quick-start/explore-ysql/).
- installed JDK version 1.8+ and maven 3.3+


### Creating the maven build file

Create a maven build file `pom.xml` and add the following content into it.

```mvn
<?xml version="1.0"?>
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
      <groupId>org.postgresql</groupId>
      <artifactId>postgresql</artifactId>
      <version>42.2.5</version>
    </dependency>
  </dependencies>

  <build>
    <plugins>
      <plugin>
        <groupId>org.apache.maven.plugins</groupId>
        <artifactId>maven-compiler-plugin</artifactId>
        <version>3.7.0</version>
        <configuration>
          <source>1.8</source>
          <target>1.8</target>
        </configuration>
      </plugin>
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

### Writing a HelloWorld CQL app

Create the appropriate directory structure as expected by maven.

```sh
$ mkdir -p src/main/java/com/yugabyte/sample/apps
```

Copy the following contents into the file `src/main/java/com/yugabyte/sample/apps/YBSqlHelloWorld.java`.

```java
package com.yugabyte.sample.apps;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;

public class YBSqlHelloWorld {
  public static void main(String[] args) {
    try {
      // Create the DB connection                                                                                
      Class.forName("org.postgresql.Driver");
      Connection connection = null;
      connection = DriverManager.getConnection(
                 "jdbc:postgresql://127.0.0.1:5433/postgres","postgres", "postgres");

      // Create table 'employee'                                                                                 
      String createStmt = "CREATE TABLE employee (id int PRIMARY KEY, " +
                                                 "name varchar, " +
                                                 "age int, " +
                                                 "language varchar);";
      connection.createStatement().execute(createStmt);
      System.out.println("Created table employee");

      // Insert a row.                                                                                           
      String insertStmt = "INSERT INTO employee (id, name, age, language)" +
                                                " VALUES (1, 'John', 35, 'Java');";
      connection.createStatement().executeUpdate(insertStmt);
      System.out.println("Inserted data: " + insertStmt);

      // Query the row and print out the result.                                                                 
      String selectStmt = "SELECT name, age, language FROM employee WHERE id = 1;";
      PreparedStatement pstmt = connection.prepareStatement(selectStmt);
      ResultSet rs = pstmt.executeQuery();
      while (rs.next()) {
          String name = rs.getString(1);
          int age = rs.getInt(2);
          String language = rs.getString(3);
          System.out.println("Query returned: " +
                             "name=" + name + ", age=" + age + ", language: " + language);
      }

      // Close the client.                                                                                       
      connection.close();
    } catch (Exception e) {
        System.err.println("Error: " + e.getMessage());
    }
  }
}
```


### Building and running the app

To build the application, just run the following command.

```sh
$ mvn package
```

To run the program, do the following.

```sh
$ java -cp "target/hello-world-1.0.jar:target/lib/*" com.yugabyte.sample.apps.YBSqlHelloWorld
```

You should see the following as the output.

```
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```
