---
title: Hibernate ORM
linkTitle: Hibernate ORM
description: Hibernate ORM support for YugabyteDB
headcontent: Hibernate ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: hibernate-orm
    parent: java-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

[Hibernate ORM](https://hibernate.org/orm/) is an Object/Relational Mapping (ORM) framework for Java applications. Hibernate ORM enables developers to easily write applications whose data outlives the appilication lifetime, Hibernate is concerned with data persistence of Relational databases.

YugabyteDB YSQL API has full compatibility with Hibernate ORM for Data persistence in Java applications. This page provides details for getting started with Hibernate ORM for connecting to YugabyteDB.

## Working with Domain Objects

In this section, we'll learn to use the Java Objects (Domain Objects) to store and retrive data from YugabyteDB Cluster.

Java developers are often required to store the Domain objects of a Java Application into the Database Tables. An Object Relational Mapping (ORM) tool is used by the developers to handle database access, it allows developers to map their object-oriented domain classes into the database tables. It simplies the CRUD operations on your domain objects and easily allow the evoluation of Domain objects to be applied to the Database tables.

[Hibernate](https://hibernate.org/orm/) is a popular ORM provider for Java applications which is widely used by Java Developers for Database access. YugabyteDB provides full support for Hiberante ORM and also can be easily used in any environment supporting Java Persistence API (JPA) including Java SE applications, and Java EE application servers connecting to YugabyteDB cluster.

### Step 1: Add the Hibernate ORM Dependency

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

Note: Hibernate ORM can be used with both [YugabyteDB JDBC driver](../yugabyte-jdbc) and [Postgres JDBC Driver](../postgres-jdbc).

### Step 2: Implementing ORM mapping for YugabyteDB

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

### Step 3: Create a DAO object for Employee object

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

### Step 4: Configure Hibernate properites

Add the hibernate configurations file `hibernate.cfg.xml` in the resources directory. Copy the following contents into `src/main/resources/hibernate.cfg.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE hibernate-configuration SYSTEM
        "http://www.hibernate.org/dtd/hibernate-configuration-3.0.dtd">

<hibernate-configuration>
    <session-factory>
        <property name="hibernate.dialect">org.hibernate.dialect.PostgreSQLDialect</property>
        <property name="hibernate.connection.driver_class">com.yugabytedb.Driver</property>
        <property name="hibernate.connection.url">jdbc:yugabytedb://localhost:5433/yugabyte</property>
        <property name="hibernate.connection.username">yugabyte</property>
        <property name="hibernate.connection.password"></property>
        <property name="hibernate.hbm2ddl.auto">update</property>
        <property name="show_sql">true</property>
        <property name="generate-ddl">true</property>
        <property name="hibernate.ddl-auto">generate</property>
        <property name="hibernate.connection.isolation">8</property>
        <property name="hibernate.current_session_context_class">thread</property>
        <property name="javax.persistence.create-database-schemas">true</property>
    </session-factory>
</hibernate-configuration>
```

The above Hibernate configuration file provides the generic set of properties that are required for configuring the Hibernate ORM for YugabyteDB.

| Hibernate Params | Description | Default |
| :---------- | :---------- | :------ |
| hibernate.dialect  | Dialect is used by hibernate to generate SQL optimized for a particular relational database | org.hibernate.dialect.PostgreSQLDialect
| hibernate.connection.driver_class | provides the JDBC Driver name  | com.yugabytedb.Driver
| hibernate.connection.url | JDBC Connection URL | jdbc:yugabytedb://localhost:5433/yugabyte
| hibernate.connection.username | Username | yugabyte
| hibernate.connection.password | Password | yugabyte
| hibernate.hbm2ddl.auto | Controls the behavior for Automatic schema generation | none

Hibernate provides an [exhaustive list of properties](https://docs.jboss.org/hibernate/orm/5.6/userguide/html_single/Hibernate_User_Guide.html#configurations-general) to configure the different features supported by the ORM. Additional details can be obtained by referring to the [Hibernate documentation](https://hibernate.org/orm/documentation/5.6/).

### Step 5: Adding the Object relational mapping

Along with properties for configuring the Hibernate ORM, `hibernate.cfg.xml` is also used for specifying the Domain objects mapping using `<mapping>` tags.

Add a mapping for `Employee` object in `hibernate.cfg.xml`

```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE hibernate-configuration SYSTEM
        "http://www.hibernate.org/dtd/hibernate-configuration-3.0.dtd">

<hibernate-configuration>
    <session-factory>
        ...
        <mapping class="com.yugabyte.hibernatedemo.model.Employee"/>
    </session-factory>
</hibernate-configuration>
```

### Step 6: Query the YugabyteDB Cluster using Hibernate ORM

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

When you run the Project, QuickStartApp.java should output something like below:

```text
Connected to the YugabyteDB Cluster successfully.
Created table employee
Inserted data: INSERT INTO employee (id, name, age, language) VALUES (1, 'John', 35, 'Java');
Query returned: name=John, age=35, language: Java
```

## Next Steps

- Explore [Scaling Java Applications](/latest/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Java Applications with YugabyteDB Cloud](/latest/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-yb-jdbc/).
