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

[Ebean ORM](https://ebean.io/) is an Object Relational Mapping (ORM) tool for Java applications. Ebean ORM is intended to be more user-friendly and understandable. Ebean's API is less complex. It does so by employing a session-less design which eliminates the concepts of detached/attached beans as well as the difficulties related with flushing/clearing. As a result, Ebean is considerably easier to learn, comprehend, and use.

YugabyteDB YSQL API has full compatibility with Ebean ORM for Data persistence in Java applications. This page provides details for getting started with Ebean ORM for connecting to YugabyteDB.


## Creating a new Java-based Play Framework project

1. First, Ensure Java Development Kit (JDK) 1.8.0—or later  and SBT 1.2.8-or later is installed before creating the project.

2. Create a new Java-Play project:
```shell
$ sbt new playframework/play-java-seed.g8
```

then specifiy the following information for the project:

```shell
This template generates a Play Java project 

name [play-java-seed]: demo-ebean
organization [com.example]: com.demo-ebean
play_version [maven(com.typesafe.play, play-exceptions, stable)]: 2.8.11
scala_version [maven(org.scala-lang, scala-library, stable)]: 2.12.8

Template applied in ./demo-ebean
```

3. Go to the project directory:

```shell
$ cd demo-ebean
```

4. Next, change the version in `build.properties` under the `project` directory to the following:

```code
sbt.version=1.2.8
```

5. Finally, download dependencies with
 ```shell
 $ sbt compile
 ```

Your new java-Play project’s folder structure is now ready to work on.

## Building the REST API using Ebean ORM with YugabyteDB 

This application will have an Employee model that retrieves employee information, such as first name, last name, and email. An EmployeeController will then store and retrieve the new employee information in the database using a Rest API.

Follow the following steps:
1. To begin using Ebean in the application, add the plugin `project/plugins.sbt` as follows:

```sbt
addSbtPlugin("com.typesafe.sbt" % "sbt-play-ebean" % "5.0.0")
```

And enable the PlayEbean plugin in the `build.sbt` file by adding `PlayEbean` as shown below:
```sbt
 lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean)
```
2. In case your default port is already in use or you want to change the port, change the settings by adding `.settings(PlayKeys.playDefaultPort := 8080)` as shown below  to the `build.sbt` file:
```sbt
 lazy val root = (project in file(".")).enablePlugins(PlayJava,PlayEbean).settings(PlayKeys.playDefaultPort := 8080)
```
3.  Next, connect to YugabyteDB by adding the following configuration to the `conf/application.conf` file:
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

Note: Ebean ORM can be used with the [YugabyteDB JDBC driver](../yugabyte-jdbc) and the [PostgreSQL JDBC Driver](../postgres-jdbc).  

4. Add the following dependencies for the Yugabyte JDBC smart-driver to the `build.sbt` file to install libraries for configuration.
```sbt
libraryDependencies += jdbc
libraryDependencies += evolutions
libraryDependencies += "com.yugabyte" % "jdbc-yugabytedb" % "42.3.3"
```

5. Create a folder in the app directory as models that will contain all the Entities we create.

6. To use this directory as a default Ebean package of classes, add the below code at the end of `conf/application.conf`.
```conf
ebean.default="models.*"
```

7. Next, create a file under `app/models/` as `Employee.java`. This is the class definition or information for the employee. Add the below code to that file:
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

8. Create a file as `app/controllers/` as `EmployeeController.java`. This file will control the flow of data of employees. It consists of methods for all API calls, such as adding an employee and retrieving employee information. Add the below code to that file as follows:
```java

package controllers;
 
import models.Employee;
import javax.persistence.*;
import play.libs.Json;
import play.mvc.*;
import java.util.ArrayList;
import java.util.List;
 
public class EmployeeController extends Controller{
 
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

9. Next, add the GET and POST API Request for the `/employees` endpoint to the `conf/routes` file. This will define the necessary method for receiving the request:

```conf
GET      /employees            controllers.EmployeeController.GetAllEmployees
POST     /employees            controllers.EmployeeController.AddEmployee(request: Request)
```
## Compiling and running the project

1. Now compile and run the server using `$ sbt compile` and `$ sbt run` in the `project` directory.

2. From here, check the functioning of the API created with curl commands as follows: 

- To create an employee using a POST request:
```shell
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email":"jsmith@xyz.com" }'
 -v -X POST -H 'Content-Type:application/json' http://localhost:8080/employees
```
- To get the details of the employees using a GET request:
```shell
$ curl  -v -X GET http://localhost:8080/employees
```
The output should look like the following:
```shell
["{'empId' = '1', firstName ='John', 'lastName' ='Smith', 'email' ='jsmith@xyz.com' }"]
```


## Next Steps

- Explore [Scaling Java Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Java applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-yb-jdbc/).
