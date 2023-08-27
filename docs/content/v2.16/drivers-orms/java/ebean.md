---
title: Use an ORM
linkTitle: Use an ORM
description: Java ORM support for YugabyteDB
headcontent: Java ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.16:
    identifier: java-orm-ebean
    parent: java-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../hibernate/" class="nav-link">
      Hibernate ORM
    </a>
  </li>

  <li >
    <a href="../ebean/" class="nav-link active">
      Ebean ORM
    </a>
  </li>

</ul>

[Ebean ORM](https://ebean.io/) is an Object Relational Mapping (ORM) tool for Java applications. The Ebean API uses a session-less design, which eliminates the concepts of detached/attached beans, as well as the difficulties related with flushing and clearing.

YugabyteDB YSQL API has full compatibility with Ebean ORM for data persistence in Java applications. This page provides details for getting started with Ebean ORM for connecting to YugabyteDB.

Ebean ORM can be used with the [YugabyteDB JDBC driver](../yugabyte-jdbc) and the [PostgreSQL JDBC Driver](../postgres-jdbc).

## CRUD operations

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Java ORM example application](../../orms/java/ysql-ebean/) page.

The following sections demonstrate how to perform common tasks required for Java-based [Play Framework](https://www.playframework.com/documentation/2.8.x/api/java/index.html) application development using the Ebean ORM.

### Create a new Java-based Play Framework project

Before you begin, ensure you have installed Java Development Kit (JDK) 1.8.0 or later and sbt 1.2.8 or later.

1. Create a new Java-Play project:

    ```sh
    sbt new playframework/play-java-seed.g8
    ```

1. When prompted, provide the following project information:

    - Name: `demo-ebean`
    - Organization: `com.demo-ebean`
    - play_version: `2.8.11`
    - scala_version: `2.12.8`

1. After the project is created, go to the project directory:

    ```sh
    cd demo-ebean
    ```

1. Change the sbt version in `build.properties` under the project directory to the following:

    ```code
    sbt.version=1.2.8
    ```

1. Download dependencies:

    ```sh
    sbt compile
    ```

Your new Java-Play project's folder structure is ready.

### Add the dependencies

To begin using Ebean in the application, do the following:

1. Add the following plugin in the `project/plugins.sbt` file:

    ```sbt
    addSbtPlugin("com.typesafe.sbt" % "sbt-play-ebean" % "5.0.0")
    ```

1. Connect to YugabyteDB by adding the following configuration to the `conf/application.conf` file:

    ```conf
    db.default.driver=com.yugabyte.Driver
    db.default.url="jdbc:yugabytedb://127.0.0.1:5433/ysql_ebean?load-balance=true"
    db.default.username=yugabyte
    db.default.password=""
    play.evolutions {
    default=true
    db.default.enabled = true
    autoApply=true
    }
    ```

1. Add the following dependency for the YugabyteDB JDBC driver to the `build.sbt` file.

    ```sbt
    libraryDependencies += "com.yugabyte" % "jdbc-yugabytedb" % "42.3.3"
    ```

1. Enable the PlayEbean plugin in the `build.sbt` file by adding `PlayEbean` as follows:

    ```sbt
    lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean)
    ```

1. If your default port is already to use, or you want to change the port, modify the settings by adding `.settings(PlayKeys.playDefaultPort := 8080)` to the `build.sbt` file as follows:

    ```sbt
    lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean).settings(PlayKeys.playDefaultPort := 8080)
    ```

### Build the REST API using Ebean ORM with YugabyteDB

The example application has an Employee model that retrieves employee information, including the first name, last name, and email. An `EmployeeController` stores and retrieves the new employee information in the database using a Rest API.

1. Create a `models` folder in the `app` directory of your project to store the entities you create.

1. To use this directory as a default Ebean package of classes, add the following code at the end of the `conf/application.conf` file:

    ```conf
    ebean.default="models.*"
    ```

1. Create a `Employee.java` file in `app/models/`. This is the class definition for the employee. Add the following code to the file:

    ```java
    package models;

    import io.ebean.Finder;
    import io.ebean.Model;
    import javax.persistence.*;
    import javax.validation.constraints.NotBlank;

    @Entity
    @Table(name = "employee")
    public class Employee extends Model{

      @Id
      @GeneratedValue(strategy = GenerationType.IDENTITY)
      @Column(columnDefinition = "serial")
      public Long empId;

      @NotBlank
      public String firstName;

      @NotBlank
      public String lastName;

      @Column(name = "emp_email")
      public String email;

      public static final Finder<Long, Employee> find = new Finder<>(Employee.class);

      @Override
      public String toString(){
          return "{'empId' = '"+empId+"', firstName ='"+firstName+"', 'lastName'      ='"+lastName+"', 'email' ='"+email+"' }";

      }
    }
    ```

1. Create an `EmployeeController.java` file in the `app/controllers/` directory. This file controls the flow of employees data. It consists of methods for all API calls, including adding an employee, and retrieving employee information. Use the `@Transactional` annotation to automatically manage the transactions in that API. Add the following code to the file:

    ```java
    package controllers;
    import models.Employee;
    import javax.persistence.*;
    import play.libs.Json;
    import play.db.ebean.Transactional;
    import play.mvc.*;
    import java.util.ArrayList;
    import java.util.List;
    public class EmployeeController extends Controller{
      @Transactional
      public Result AddEmployee(Http.Request request){
          Employee employee=Json.fromJson(request.body().asJson(),Employee.class);
          employee.save();
          return ok(Json.toJson(employee.toString()));
      }
      public Result GetAllEmployees(){
          List <Employee> employees = Employee.find.all();
          List<String> employeesList = new ArrayList<String>();
          for(int index=0;index<employees.size();index++){
              employeesList.add(employees.get(index).toString());
          }
          return ok(Json.toJson(employeesList));
      }
    }
    ```

1. Add the GET and POST API Request for the `/employees` endpoint to the `conf/routes` file. This defines the method needed for receiving the request:

    ```conf
    GET      /employees            controllers.EmployeeController.GetAllEmployees
    POST     /employees            controllers.EmployeeController.AddEmployee(request: Request)
    ```

### Compile and run the project

To run the application and insert a new row, execute the following steps:

1. Compile and run the server in the `project` directory using the following commands:

   ```sh
   sbt compile
   ```

   ```sh
   sbt run
   ```

1. Create an employee using a POST request:

   ```sh
   curl --data '{ "firstName" : "John", "lastName" : "Smith", "email":"jsmith@xyz.com" }' \
   -v -X POST -H 'Content-Type:application/json' http://localhost:8080/employees
   ```

1. Get the details of the employees using a GET request:

   ```sh
   curl  -v -X GET http://localhost:8080/employees
   ```

    The output should look like the following:

    ```output
    ["{'empId' = '1', firstName ='John', 'lastName' ='Smith', 'email' ='jsmith@xyz.com' }"]
    ```

## Learn more

- Build Java applications using [Hibernate ORM](../hibernate/)
- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)