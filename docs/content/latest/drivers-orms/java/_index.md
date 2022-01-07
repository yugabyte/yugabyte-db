---
title: Java
headerTitle: Java
linkTitle: Java
description: Java Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: java-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---
For Java application, this page provides list of all the Drivers and ORMs which are supported for connecting to YugabyteDB YSQL API. `YugabyteDB JDBC Driver` is the recommended Java Driver for YSQL.

Recommended projects for implementing Java Applications for YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [YugabyteDB JDBC Driver](yugabyte-jdbc) | JDBC Driver | Full |
| [Hibernate](hibernate) | ORM |  Full |
| [Spring Data YugabyteDB](/latest/integrations/spring-framework/sdyb/) | Framework |  Full |

## Build a Hello World App

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/java/ysql-yb-jdbc) in the Quick Start section.

## Usage Examples

For fully runnable code snippets and exaplantion for common operations, see the specific Java driver ([YugabyteDB JDBC](yugabyte-jdbc)) section.

## Building a Java Application for YugabyteDB

### Install the Java Development Kit (JDK)

Make sure that your system has JDK 8 or later installed. For more information on how to check your version of Java and install the JDK, see the [AdoptOpenJDK Installation Page](https://adoptopenjdk.net/installation.html).

### Create a Java Project

Java Projects can be created using Maven or Gradle Software project management tools. We recommend using an integrate develpoment environemnt (IDE) such as Intellij IDEA or Eclipse IDE for conveniently configuring Maven or Gradle to build and run your project.

If you are not using an IDE, see [Building Maven](https://maven.apache.org/guides/development/guide-building-maven.html) or [Creating New Gradle Project](https://docs.gradle.org/current/samples/sample_building_java_applications.html) for more information on how to setup a Java project.

#### Add the YugabyteDB JDBC Driver Dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jdbc-yugabytedb</artifactId>
  <version>42.3.0</version>
</dependency>

<!-- https://mvnrepository.com/artifact/com.zaxxer/HikariCP -->
<dependency>
  <groupId>com.zaxxer</groupId>
  <artifactId>HikariCP</artifactId>
  <version>4.0.3</version>
</dependency>
```

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'com.yugabyte:jdbc-yugabytedb:42.3.0'
implementation 'com.zaxxer:HikariCP:4.0.3'
```

### Create a YugabyteDB Cluster

You can also setup a standalone YugabyteDB cluster by following the [install YugabyteDB Steps](/latest/quick-start/install/macos).

Alternatively, Set up a Free tier Cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/). The free cluster provides a fully functioning YugabyteDB cluster deployed to the cloud region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing the Java Applications with YugabyteDB database. Complete the steps for [creating a free tier cluster](latest/yugabyte-cloud/cloud-quickstart/qs-add/).

### Connect to your Cluster

After seeting up the dependenices, we implement the Java client application that uses the YugabyteDB JDBC driver to connect to your YugabyteDB cluster and run query on the sample data.

We will setup the driver properties for pass in the credentials and SSL Certs for connecting to your cluster. Java Apps can connect to and query the YugabyteDB database using the `java.sql.DriverManager` class. All the JDBC interfaces required for working with YugabyteDB database will be part of `java.sql.*` package.

Use the `DriverManager.getConnection` method for getting connection object for the YugabyteDB Database which can be used for performing DDLs and DMLs against the database.

Example JDBC URL for connecting to YugabyteDB can be seen below.

```java
string yburl = "jdbc://yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true"
DriverManager.getConnection(yburl);
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| hostname  | hostname of the YugabyteDB instance | localhost
| port |  Listen port for YSQL | 5433
| database | database name | yugabyte
| user | user for connecting to the database | yugabyte
| password | password for connecting to the database | yugabyte
| load-balance | enables uniform load balancing | true

Example JDBC URL for connecting to YugabyteDB cluster enabled with on the wire SSL encryption.

```java
string yburl = "jdbc://yugabytedb://hostname:port/database?user=yugabyte&password=yugabyte&load-balance=true&ssl=true&sslmode=verify-full&sslrootcert=~/.postgresql/root.crt"
Connection conn = DriverManager.getConnection(yburl);
```

| JDBC Params | Description | Default |
| :---------- | :---------- | :------ |
| ssl  | Enable SSL client connection   | false
| sslmode | SSL mode  | require
| sslrootcert | path to the root certificate on your computer | ~/.postgresql/

If you have created Free tier cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/), [Follow the steps](/latest/yugabyte-cloud/cloud-connect/connect-applications/) to download the Credentials and SSL Root certificate.

### Query the YugabyteDB Cluster from Your Application

Next, Create a new Java class called `QuickStartApp.java` in the base package directory of your project. Copy the sample code below in order to setup a YugbyteDB Tables and query the Table contents from the java client. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.

```java
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.Scanner;

public class QuickStartApp {
  public static void main(String[] args) throws ClassNotFoundException, SQLException {
    Class.forName("com.yugabyte.Driver");
    String yburl = "jdbc:yugabytedb://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte&load-balance=true";
    Connection conn = DriverManager.getConnection(yburl);
    Statement stmt = conn.createStatement();
    try {
        System.out.println("Connected to the YugabyteDB Cluster successfully.");
        stmt.execute("DROP TABLE IF EXISTS employee");
        stmt.execute("CREATE TABLE IF NOT EXISTS employee" +
                    "  (id int primary key, name varchar, age int, language text)");
        System.out.println("Created table employee");

        String insertStr = "INSERT INTO employee VALUES (1, 'John', 35, 'Java')";
        stmt.execute(insertStr);
        System.out.println("EXEC: " + insertStr);

        ResultSet rs = stmt.executeQuery("select * from employee");
        while (rs.next()) {
          System.out.println(String.format("Query returned: name = %s, age = %s, language = %s",
                                          rs.getString(2), rs.getString(3), rs.getString(4)));
        }
    } catch (SQLException e) {
      System.err.println(e.getMessage());
    }
  }
}
```

When you run the Project, `QuickStartApp.java` should output something like below:

```text
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

if you receive no output or error, check whether you included the proper connection string in your java class with the right credentials.

After completing this steps, you should have a working Java app that uses YugabyteDB JDBC driver for connecting to your cluster, setup tables, run query and print out results.

## Working with Domain Objects (ORMs)

In the previous section, you ran a SQL query on a sample table and displayed the results set. In this section, we'll lear to use the Java Objects (Domain Objects) to store and retrive data from YugabyteDB Cluster.

Java developers are often required to store the Domain objects of a Java Application into the Database Tables. An Object Relational Mapping (ORM) tool is used by the developers to handle database access, it allows developeres to map their object-oriented domain classes into the database tables. It simplies the CRUD operations on your domain objects and easily allow the evoluation of Domain objects to applied to the Database tables.

[Hibernate](https://hibernate.org/orm/) is a popular ORM provider for Java applications which is widely used by Java Developers for Database access. YugabyteDB provides full support for Hiberante ORM and also can be easily used in any environment supporting Java Persistence API (JPA) including Java SE applications, and Java EE application servers connecting to YugabyteDB cluster.

### Add the Hibernate ORM Dependency

If you are using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your `pom.xml` of your project.

```xml
<dependency>
    <groupId>org.hibernate</groupId>
    <artifactId>hibernate-core</artifactId>
    <version>5.4.19.Final</version>
</dependency>

<dependency>
    <groupId>org.hibernate</groupId>
    <artifactId>hibernate-annotations</artifactId>
    <version>3.5.6-Final</version>
</dependency>
```

If you are using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'org.hibernate:hibernate-core:5.4.19.Final'
implementation 'org.hibernate:hibernate-annotations:3.5.6-Final'
```

### Implementing ORM mapping for YugabyteDB

Create a file called `Employee.java` in the base package directory of your project and add the following code for a class that includes the following fields, setters and getters,

```java
@Entity
@Table(name = "employee")
public class Employee {

  @Id
  Integer id;
  String name;
  Integer age;
  String language;

  // Setters and Getters

}
```

Create a Data Access Object (DAO) `EmployeeDAO.java` in the base package directory. DAO object is used for implementing the basic CRUD operations for the Domain object `Employee.java`. Copy the sample below sample code into your project,

```java
import org.hibernate.Session;

public class EmployeeDAO {

  Session hibernateSession;

  public EmployeeDAO (Session session) {
    hibernateSession = session;
  }

  public void save(final Employee employeeEntity) {
    Transaction transaction = session.beginTransaction();
        try {
            session.save(entity);
            transaction.commit();
        } catch(RuntimeException rte) {
            transaction.rollback();
        }
        session.close();
  }

  public Optional<Employee> findById(final Integer id) {
    return Optional.ofNullable(session.get(Emplyee.class, id));
  }
}
```
<!-- Explain the above hibernate code especially hibernate session -->

Add the hibernate configurations file `hibernate.cfg.xml` in the resources directory. Copy the following contents into `src/main/resources/hibernate.cfg.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE hibernate-configuration SYSTEM
        "http://www.hibernate.org/dtd/hibernate-configuration-3.0.dtd">

<hibernate-configuration>
    <session-factory>
        <property name="hibernate.dialect">org.hibernate.dialect.PostgreSQLDialect</property>
        <property name="hibernate.connection.driver_class">org.postgresql.Driver</property>
        <property name="hibernate.connection.url">jdbc:postgresql://localhost:5433/yugabyte</property>
        <property name="hibernate.connection.username">yugabyte</property>
        <property name="hibernate.connection.password"></property>
        <property name="hibernate.hbm2ddl.auto">update</property>
        <property name="show_sql">true</property>
        <property name="generate-ddl">true</property>
        <property name="hibernate.ddl-auto">generate</property>
        <property name="hibernate.connection.isolation">8</property>
        <property name="hibernate.current_session_context_class">thread</property>
        <property name="javax.persistence.create-database-schemas">true</property>
        <mapping class="com.yugabyte.hibernatedemo.model.Employee"/>
    </session-factory>
</hibernate-configuration>
```

Next, Create a new Java class called `QuickStartOrmApp.java` in the base package directory of your project. Copy the sample code below in order to query the Table contents from the java client using Hibernate ORM. Ensure you replace the connection string `yburl` with credentials of your cluster and SSL certs if required.

```java
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.Scanner;
import org.hibernate.Session;
import org.hibernate.SessionFactory;

public class QuickStartOrmApp {


  public static void main(String[] args) throws ClassNotFoundException, SQLException {

    SessionFactory sessionFactory = HibernateUtil.getSessionFactory();
    Session session = sessionFactory.openSession();

    try {
          System.out.println("Connected to the YugabyteDB Cluster successfully.");
          EmplyeeDAO employeeDAO = new EmployeeDAO(session);
          // Save an employee
          employeeDAO.save(new Employee());

          // Find the emplyee
          Employee employee = employeeDAO.findByID(1);
          System.out.println("Query Returned:" + employee.toString());
        }
    } catch (SQLException e) {
      System.err.println(e.getMessage());
    }
  }
}
```

```
When you run the Project, QuickStartApp.java should output something like below:

Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

## Next Steps

- [Learn to Develop Spring Boot Applications using YugabyteDB JDBC Driver](/latest/integrations/spring-framework/sdyb/)
- Learn to Develop Micronot Applications using YugabyteDB JDBC Driver