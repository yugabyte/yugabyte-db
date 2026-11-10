---
title: Hibernate ORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Java Hibernate ORM support for YugabyteDB
headcontent: Java ORM support for YugabyteDB
menu:
  stable_develop:
    identifier: java-orm
    parent: java-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../hibernate/" class="nav-link active">
      Hibernate ORM
    </a>
  </li>

  <li >
    <a href="../ebean/" class="nav-link ">
      Ebean ORM
    </a>
  </li>

  <li >
    <a href="../mybatis/" class="nav-link ">
      MyBatis
    </a>
  </li>

</ul>

[Hibernate ORM](https://hibernate.org/orm/) is an Object/Relational Mapping (ORM) framework for Java applications. Hibernate ORM is concerned with data persistence of relational databases, and enables developers to write applications whose data outlives the application lifetime.

YugabyteDB YSQL API has full compatibility with Hibernate ORM for Data persistence in Java applications. This page provides details for getting started with Hibernate ORM for connecting to YugabyteDB.

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Java ORM example application](../../orms/java/ysql-hibernate/) page.

The following sections demonstrate how to perform common tasks required for Java application development using the Hibernate ORM.

### Step 1: Add the Hibernate ORM dependency

If you're using [Maven](https://maven.apache.org/guides/development/guide-building-maven.html), add the following to your project's `pom.xml` file.

```xml
<dependency>
    <groupId>org.hibernate</groupId>
    <artifactId>hibernate-core</artifactId>
    <version>5.4.19.Final</version>
</dependency>

<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>jdbc-yugabytedb</artifactId>
    <version>{{< version-driver-java >}}</version>
</dependency>
```

If you're using [Gradle](https://docs.gradle.org/current/samples/sample_building_java_applications.html), add the following dependencies to your `build.gradle` file:

```java
implementation 'org.hibernate:hibernate-core:5.4.19.Final'
implementation 'com.yugabyte:jdbc-yugabytedb:{{< version-driver-java >}}'
```

Note: Hibernate ORM can be used with the [YugabyteDB JDBC driver](../yugabyte-jdbc) and the [PostgreSQL JDBC Driver](../postgres-jdbc).

### Step 2: Implementing ORM mapping for YugabyteDB

Create `Employee.java` in the `com.yugabyte.hibernatedemo` package. The class maps to the `employee` table.

```java
package com.yugabyte.hibernatedemo;

import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;

@Entity
@Table(name = "employee")
public class Employee {

  @Id
  private Integer id;
  private String name;
  private Integer age;
  private String language;

  public Employee() {
  }

  public Employee(Integer id, String name, Integer age, String language) {
    this.id = id;
    this.name = name;
    this.age = age;
    this.language = language;
  }

  public Integer getId() {
    return id;
  }

  public void setId(Integer id) {
    this.id = id;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public Integer getAge() {
    return age;
  }

  public void setAge(Integer age) {
    this.age = age;
  }

  public String getLanguage() {
    return language;
  }

  public void setLanguage(String language) {
    this.language = language;
  }

  @Override
  public String toString() {
    return "name=" + name + ", age=" + age + ", language: " + language;
  }
}
```

### Step 3: Create a DAO object for Employee object

Create `EmployeeDAO.java` in the `com.yugabyte.hibernatedemo` package. The DAO implements save and lookup for `Employee`. The caller opens and closes the Hibernate session.

```java
package com.yugabyte.hibernatedemo;

import java.util.Optional;

import org.hibernate.Session;
import org.hibernate.Transaction;

public class EmployeeDAO {

  Session hibernateSession;

  public EmployeeDAO(Session session) {
    hibernateSession = session;
  }

  public void save(final Employee employeeEntity) {
    Transaction transaction = hibernateSession.beginTransaction();
    try {
      hibernateSession.save(employeeEntity);
      transaction.commit();
    } catch (RuntimeException rte) {
      transaction.rollback();
      throw rte;
    }
  }

  public Optional<Employee> findById(final Integer id) {
    return Optional.ofNullable(hibernateSession.get(Employee.class, id));
  }
}
```

### Step 4: Configure Hibernate properties

Add the hibernate configurations file `hibernate.cfg.xml` to the resources directory, and copy the following contents into the file.

```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE hibernate-configuration SYSTEM
        "http://www.hibernate.org/dtd/hibernate-configuration-3.0.dtd">

<hibernate-configuration>
    <session-factory>
        <property name="hibernate.dialect">org.hibernate.dialect.PostgreSQLDialect</property>
        <property name="hibernate.connection.driver_class">com.yugabyte.Driver</property>
        <property name="hibernate.connection.url">jdbc:yugabytedb://localhost:5433/yugabyte</property>
        <property name="hibernate.connection.username">yugabyte</property>
        <property name="hibernate.connection.password">yugabyte</property>
        <property name="hibernate.hbm2ddl.auto">update</property>
        <property name="hibernate.connection.isolation">8</property>
        <property name="hibernate.current_session_context_class">thread</property>
    </session-factory>
</hibernate-configuration>
```

The Hibernate configuration file provides the generic set of properties that are required for configuring the Hibernate ORM for YugabyteDB.

| Hibernate Parameter | Description | Default |
| :---------- | :---------- | :------ |
| hibernate.dialect  | Dialect to use to generate SQL optimized for a particular database | org.hibernate.dialect.PostgreSQLDialect
| hibernate.connection.driver_class | JDBC Driver name  | com.yugabyte.Driver
| hibernate.connection.url | JDBC Connection URL | jdbc:yugabytedb://localhost:5433/yugabyte
| hibernate.connection.username | Username | yugabyte
| hibernate.connection.password | Password | yugabyte
| hibernate.hbm2ddl.auto | Behaviour for automatic schema generation | update

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
        <mapping class="com.yugabyte.hibernatedemo.Employee"/>
    </session-factory>
</hibernate-configuration>
```

### Step 6: Create the SessionFactory

Create `HibernateUtil.java` in the `com.yugabyte.hibernatedemo` package. `configure` loads `hibernate.cfg.xml` from the classpath.

```java
package com.yugabyte.hibernatedemo;

import org.hibernate.SessionFactory;
import org.hibernate.cfg.Configuration;

public class HibernateUtil {

  private static final SessionFactory sessionFactory =
      new Configuration().configure("hibernate.cfg.xml").buildSessionFactory();

  public static SessionFactory getSessionFactory() {
    return sessionFactory;
  }
}
```

### Step 7: Query the YugabyteDB cluster using Hibernate ORM

Create `QuickStartOrmApp.java` in the `com.yugabyte.hibernatedemo` package. The application stores one employee and reads that row back. Set the host, user, and password in `hibernate.cfg.xml`.

```java
package com.yugabyte.hibernatedemo;

import java.util.NoSuchElementException;

import org.hibernate.Session;
import org.hibernate.SessionFactory;

public class QuickStartOrmApp {

  public static void main(String[] args) {
    SessionFactory sessionFactory = HibernateUtil.getSessionFactory();

    try (Session session = sessionFactory.openSession()) {
      System.out.println("Connected to the YugabyteDB Cluster successfully.");
      EmployeeDAO employeeDAO = new EmployeeDAO(session);
      employeeDAO.save(new Employee(1, "John", 35, "Java"));

      Employee employee = employeeDAO.findById(1)
          .orElseThrow(NoSuchElementException::new);
      System.out.println("Query Returned: " + employee);
    } finally {
      sessionFactory.close();
    }
  }
}
```

Running `QuickStartOrmApp` prints:

```text
Connected to the YugabyteDB Cluster successfully.
Query Returned: name=John, age=35, language: Java
```

## Learn more

- Build Java applications using [Ebean ORM](../ebean/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
