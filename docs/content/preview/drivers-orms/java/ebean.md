---
title: Ebean ORM
linkTitle: Ebean ORM
description: Ebean ORM support for YugabyteDB
headcontent: Ebean ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Java ORMs
    identifier: ebean-orm
    parent: java-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/java/hibernate" class="nav-link">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Hibernate ORM
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/java/ebean/" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Ebean ORM
    </a>
  </li>

</ul>

[Ebean ORM](https://ebean.io/) is an Object Relational Mapping (ORM) tool for Java applications. The Ebean API uses a session-less design, which eliminates the concepts of detached/attached beans, as well as the difficulties related with flushing and clearing.

YugabyteDB YSQL API has full compatibility with Ebean ORM for data persistence in Java applications. This page provides details for getting started with Ebean ORM for connecting to YugabyteDB.

Ebean ORM can be used with the [YugabyteDB JDBC driver](../yugabyte-jdbc) and the [PostgreSQL JDBC Driver](../postgres-jdbc).

## CRUD operations with Ebean ORM

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in the [Build an application](/preview/quick-start/build-apps/java/ysql-ebeans/) page under the Quick start section.

The following sections demonstrate how to perform common tasks required for Java-based [Play Framework](https://www.playframework.com/documentation/2.8.x/api/java/index.html) application development using the Ebean ORM.

### Create a new Java-based Play Framework project

Before you begin, ensure you have installed Java Development Kit (JDK) 1.8.0 or later and sbt 1.2.8 or later.

1. Create a new Java-Play project:

    ```shell
    $ sbt new playframework/play-java-seed.g8
    ```

1. When prompted, provide the following project information:

    - Name: `demo-ebean`
    - Organization: `com.demo-ebean`
    - play_version: `2.8.11`
    - scala_version: `2.12.8`

1. After the project is created, go to the project directory:

    ```shell
    $ cd demo-ebean
    ```

1. Change the SBT version in `build.properties` under the project directory to the following:

    ```code
    sbt.version=1.2.8
    ```

1. Download dependencies:

    ```shell
    $ sbt compile
    ```

Your new Java-Play project's folder structure is now ready to work on.

### Add the dependencies

Do the following:

1. To begin using Ebean in the application, add the plugin `project/plugins.sbt` as follows:

    ```sbt
    addSbtPlugin("com.typesafe.sbt" % "sbt-play-ebean" % "5.0.0")
    ```

1. Enable the PlayEbean plugin in the `build.sbt` file by adding `PlayEbean` as follows:

    ```sbt
    lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean)
    ```

1. If your default port is already in use or you want to change the port, change the settings by adding `.settings(PlayKeys.playDefaultPort := 8080)` to the `build.sbt` file as follows:

    ```sbt
    lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean).settings(PlayKeys.playDefaultPort := 8080)
    ```

1. Connect to YugabyteDB by adding the following configuration to the `conf/application.conf` file:

    ```conf
    db.default.driver=com.yugabyte.Driver
    db.default.url="jdbc:yugabytedb://127.0.0.1:5433/ysql_ebeans?load-balance=true"
    db.default.username=yugabyte
    db.default.password=""
    play.evolutions {
    default=true
    db.default.enabled = true
    autoApply=true
    }
    ```

1. Add the following dependencies for the Yugabyte JDBC smart-driver to the `build.sbt` file to install libraries for configuration.

    ```sbt
    libraryDependencies += jdbc
    libraryDependencies += evolutions
    libraryDependencies += "com.yugabyte" % "jdbc-yugabytedb" % "42.3.3"
    ```

### Build the REST API using Ebean ORM with YugabyteDB

The example application has an Employee model that retrieves employee information, such as first name, last name, and email. An `EmployeeController` stores and retrieves the new employee information in the database using a Rest API.

Do the following:

1. Create a `models` folder in the app directory. It is used to contain the entities you create.

1. To use this directory as a default Ebean package of classes, add the following code at the end of `conf/application.conf`:

    ```conf
    ebean.default="models.*"
    ```

1. Create a `Employee.java` file in `app/models/`. This is the class definition or information for the employee. Add the following code to the file:

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

1. Create a `EmployeeController.java` file in `app/controllers/`. This file controls the flow of employees data. It consists of methods for all API calls, such as adding an employee and retrieving employee information. Using the annotation `@Transactional` over the APIs gives the feature of automatically managing the transaction in that API. Add the following code to the file:

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

1. Add the GET and POST API Request for the `/employees` endpoint to the `conf/routes` file. This will define the necessary method for receiving the request:

    ```conf
    GET      /employees            controllers.EmployeeController.GetAllEmployees
    POST     /employees            controllers.EmployeeController.AddEmployee(request: Request)
    ```

### Compile and run the project

1. Compile and run the server using `$ sbt compile` and `$ sbt run` in the `project` directory.

1. Create an employee using a POST request:

    ```shell
    $ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email":"jsmith@xyz.com" }' \
    -v -X POST -H 'Content-Type:application/json' http://localhost:8080/employees
    ```

1. Get the details of the employees using a GET request:

    ```shell
    $ curl  -v -X GET http://localhost:8080/employees
    ```

The output should look like the following:

  ```output.json
  ["{'empId' = '1', firstName ='John', 'lastName' ='Smith', 'email' ='jsmith@xyz.com' }"]
  ```

## Next steps

- Explore [Scaling Java Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Java applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-yb-jdbc/).
