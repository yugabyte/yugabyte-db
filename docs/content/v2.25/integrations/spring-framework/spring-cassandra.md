---
title: Spring Data Cassandra
linkTitle: Spring Data Cassandra
description: Spring Data Cassandra
aliases:
menu:
  preview_integrations:
    identifier: spring-cassandra
    parent: spring-framework
    weight: 580
type: docs
---

This document describes how to use Spring Data Cassandra and reactive Spring Data Cassandra with YCQL.

## Spring Data Cassandra and YCQL

YCQL provides Cassandra wire-compatible query language for client applications to query the YugabyteDB database. YCQL is integrated with the Spring Data Cassandra project and supports POJO-based annotations, data templates, repositories, and so on.

The following is a non-exhaustive list of supported features:

- Yugabyte Java driver for YCQL 3.x and 4.x drivers.
- Automatic implementation of Repository interfaces including support for custom query methods.
- Build repositories based on common Spring Data interfaces.
- Synchronous, reactive, and asynchronous YCQL operations.

### Project Dependencies

Spring Data Cassandra projects are bundled with the Apache Cassandra Java driver. To enhance performance, it is recommended to replace this driver with the Yugabyte Java driver for YCQL.

**Yugabyte Java Driver YCQL 3.x**

```xml
<dependency>
  <groupId>org.springframework.data</groupId>
  <artifactId>spring-data-cassandra</artifactId>
  <version>2.2.12.RELEASE</version>
  <exclusions>
​   <exclusion>
​     <groupId>com.datastax.cassandra</groupId>
​     <artifactId>cassandra-driver-core</artifactId>
​   </exclusion>
  </exclusions>
</dependency>

<!-- YCQL Driver -->
<dependency>
 <groupId>com.yugabyte</groupId>
 <artifactId>cassandra-driver-core</artifactId>
 <version>3.8.0-yb-6</version>
</dependency>
```

The following shows how to exclude and add dependencies via Gradle:

```gradle
dependencies {
  compile('org.springframework.data:spring-data-cassandra:2.2.12.RELEASE') {
  exclude group: "com.datastax.cassandra", name: "cassandra-driver-core"
}
  compile('com.yugabyte:cassandra-driver-core:3.8.0-yb-6')
}
```

**Yugabyte Java Driver YCQL 4.x**

```xml
<dependency>
  <groupId>org.springframework.data</groupId>
  <artifactId>spring-data-cassandra</artifactId>
  <version>3.0.6.RELEASE</version>
  <exclusions>
​   <exclusion>
​     <groupId>com.datastax.oss</groupId>
​     <artifactId>java-driver-core</artifactId>
​   </exclusion>
  </exclusions>
</dependency>

<!-- YCQL Driver -->
<dependency>
 <groupId>com.yugabyte</groupId>
 <artifactId>java-driver-core</artifactId>
 <version>4.6.0-yb-6</version>
</dependency>
```

The following shows how to exclude and add dependencies via Gradle:

```gradle
dependencies {
  compile('org.springframework.data:spring-data-cassandra:3.0.6.RELEASE') {
  exclude group: "com.datastax.oss", name: "java-driver-core"
}
  compile('com.yugabyte:java-driver-core:4.6.0-yb-6')
}
```

Yugabyte Java driver for YCQL provides support for single hop fetch which enables topology awareness, shard awareness data access using Spring Data Cassandra templates and repositories. In addition, Yugabyte Java drivers for YCQL support distributed transactions in Spring Boot applications using custom query methods.

### Sample Spring Boot Project

A sample Spring boot project is available at <https://github.com/yugabyte/spring-ycql-demo>. The following steps demonstrate how to incrementally build a Spring boot application using Spring Data Cassandra and YCQL.

#### Use Spring Initializer

1. Navigate to [https://start.spring.io](https://start.spring.io/) to create a new Spring boot project. Select the following dependencies for implementing the Spring boot application:
    - Spring Boot Data Cassandra
    - Java 8

1. Update the Maven dependencies to use Yugabyte Java driver for YCQL, as follows:

    ```xml
      <dependency>
        <groupId>org.springframework.boot</groupId>
        <artifactId>spring-boot-starter-data-cassandra</artifactId>
        <exclusions>
      ​   <exclusion>
      ​     <groupId>com.datastax.oss</groupId>
      ​     <artifactId>java-driver-core</artifactId>
      ​   </exclusion>
        </exclusions>
      </dependency>
      <dependency>
        <groupId>com.yugabyte</groupId>
        <artifactId>java-driver-core</artifactId>
        <version>4.6.0-yb-6</version>
      </dependency>
    ```

1. Configure the Spring boot application to connect to YugabyteDB cluster using the following properties in the `application.properties` file:

    | Property | Description |
    | :------- | :---------- |
    | spring.data.cassandra.keyspace-name | YCQL keyspace |
    | spring.data.cassandra.contact-points | YugabyteDB T-servers. Comma-separated list of IP addresses or DNS |
    | spring.data.cassandra.port | YCQL port |
    | spring.data.cassandra.local-datacenter | Datacenter that is considered "local". Contact points should be from this datacenter. |

#### Set Up Domain Objects and Repositories for YCQL Tables

Create a `Customer` object to provide data access to allow Spring boot applications to manage first and last names of customers in a YugabyteDB table. To represent the object-table mapping to this data at the application level, you need to create a `Customer` Java class, as follows:

```java
package com.yugabyte.example.ycqldataaccess;

public class Customer {

  private long id;
  private String firstName, lastName;

  public Customer(long id, String firstName, String lastName) {
    this.id = id;
    this.firstName = firstName;
    this.lastName = lastName;
  }

  @Override
  public String toString() {
    return String.format(
​    "Customer[id=%d, firstName='%s', lastName='%s']",
​    id, firstName, lastName);
  }

 // define getters and setters
 // ...
}
```

#### Store and Retrieve Data

Spring Data Cassandra provides the `CassandraRepositories` interface that removes all the boilerplate code and simplifies definition of CRUD operations against YugabyteDB tables. Most of the YugabyteDB connection handling, exception handling, and general error handling is performed by repositories, leaving you to implement the business logic.

Create the `CustomerRepository` interface, as follows:

```java
package com.yugabyte.example.ycqldataaccess;

import java.util.Optional;
import org.springframework.data.cassandra.repository.CassandraRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface CustomerRepository extends CassandraRepository<Customer, String> {
  Optional<Customer> findByFirstName(String firstName);
}
```

The following `YCQLDataAccessApplication.java` is a demonstration of a class that can store and retrieve data from YugabyteDB tables using Cassandra repositories:

```java
package com.yugabyte.example.ycqldataaccess;

import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.data.cassandra.repository.config.EnableCassandraRepositories;
import com.datastax.oss.driver.api.core.CqlSession;

@SpringBootApplication
@EnableCassandraRepositories(basePackageClasses = Customer.class)
public class YcqlDataAccesssApplication implements CommandLineRunner {
  private static final Logger log = LoggerFactory.getLogger
                                    (YcqlDataAccesssApplication.class);
  @Autowired
  CustomerRepository customerRepository;

  @Autowired
  CqlSession ycqlSession;

  public static void main(String[] args) {
​   SpringApplication.run(YcqlDataAccesssApplication.class, args);
  }

  @Override
  public void run(String... args) throws Exception {
​   log.info("Creating Keyspace");
​   ycqlSession.execute("CREATE KEYSPACE IF NOT EXISTS demo;");
​   log.info("Creating table");
​   ycqlSession.execute("CREATE TABLE demo.customer
      (\n" + "  id INT PRIMARY KEY,\n" + " firstName text,\n" \+
       " lastName text\n" + ") WITH default_time_to_live = 0\n"
      \+ " AND transactions = {'enabled': 'true'};");

​    // Split the array of whole names into an array of first-last names
​    List<Object[]> splitUpNames = Arrays.asList(
                    "Tim Bun", "Mike Dean", "Alan Row", "Josh Rambo")
                    .stream().map(name -> name.split(" ")).collect(Collectors.toList());
​
    // Use a Java 8 stream to print out each tuple of the list
​   splitUpNames.forEach(name -> {
​      int id = 1;
​      log.info(String.format("Inserting customer record for %s %s", name[0], name[1]));
​      customerRepository.save(new Customer(id, (String) name[0], (String) name[1]));
​      id++;
​   });
​   log.info("Querying for customer records where first_name = 'Josh':");
​   log.info(customerRepository.findByFirstName("Josh").toString());
  }
}
```

The `CustomerRepository` type is autowired in `YcqlDataAccesssApplication` which allows the insert of new records into the `customer` table using the `customerRepository.save()` method.

You can retrieve the data using the `customerRepository.findByFirstName()` method which returns records based on the `firstName` column. The implementation of the method is supplied by Spring at runtime.

#### Build an Executable JAR

The next step is to run the Spring application from the command line using Maven, as follows:

```shell
./mvnw spring-boot:run
```

Alternatively, you can build an uber JAR file by using the following command:

```shell
./mvnw clean package
```

Then you run the JAR file using the following command:

```shell
java -jar target/ycqldataaccess-1.0.0.jar
```

Expect output similar to the following:

```output
INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Started YcqlDataAccesssApplication in 1.7 seconds (JVM running for 2.139)

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Creating Keyspace

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Creating table

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Inserting customer record for Tim Bun

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Inserting customer record for Mike Dean

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Inserting customer record for Alan Row

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Inserting customer record for Josh Rambo

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Querying for customer records where first_name = 'Josh':

INFO 28010 --- [main] c.y.e.y.YcqlDataAccesssApplication    : Optional[Customer[id=1, firstName='Josh', lastName='Rambo']]
```

## Spring Data Reactive Repositories and YCQL

YCQL API is compatible with Spring Data reactive repositories for Apache Cassandra.

Using Spring WebFlux and Spring Data reactive repositories, you can implement fully reactive Spring Microservices with YCQL API.

### Project Dependencies

Reactive Spring Data Cassandra projects are bundled with the Apache Cassandra Java driver. To enhance performance, it is recommended to replace this driver with Yugabyte Java driver for YCQL, as follows:

```xml
<dependency>
  <groupId>org.springframework.boot</groupId>
  <artifactId>spring-boot-starter-data-cassandra-reactive</artifactId>
  <exclusions>
    <exclusion>
        <groupId>com.datastax.oss</groupId>
        <artifactId>java-driver-core</artifactId>
    </exclusion>
  </exclusions>
</dependency>

<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>java-driver-core</artifactId>
  <version>4.6.0-yb-6</version>
</dependency>
```

### Sample Spring Boot Project for Reactive

A sample Spring boot project is available at <https://github.com/yugabyte/spring-reactive-ycql-client>. The following steps show how to incrementally build a Spring boot application using Spring Data Cassandra and YCQL.

#### Use Spring Initializer

1. Navigate to [https://start.spring.io](https://start.spring.io/) to create a new Spring boot project. Select the following dependencies for implementing the Spring boot application:
    - Spring Boot Data Cassandra Reactive
    - Java 8

1. Update the Maven dependencies to use the Yugabyte Java driver for YCQL, as follows:

    ```xml
    <dependency>
      <groupId>org.springframework.boot</groupId>
      <artifactId>spring-boot-starter-data-cassandra-reactive</artifactId>
      <exclusions>
        <exclusion>
          <groupId>com.datastax.oss</groupId>
          <artifactId>java-driver-core</artifactId>
        </exclusion>
      </exclusions>
    </dependency>

    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>java-driver-core</artifactId>
      <version>4.6.0-yb-6</version>
    </dependency>
    ```

1. Configure the Spring boot application to connect to YugabyteDB cluster using the following properties in the `application.properties` file:

    | Property | Description |
    | :------- | :---------- |
    | spring.data.cassandra.keyspace-name | YCQL keyspace |
    | spring.data.cassandra.contact-points | YugabyteDB T-servers. Comma-separated list of IP addresses or DNS |
    | spring.data.cassandra.port | YCQL port |
    | spring.data.cassandra.local-datacenter | Datacenter that is considered "local". Contact points should be from this datacenter. |

#### Set Up Domain Objects and Repositories for YugabyteDB Tables

Create a `Customer` object to provide data access to allow Spring boot applications to manage first and last names of customers in a YugabyteDB table. To represent the object-table mapping to this data at the application level, you need to create a `Customer` Java class, as follows:

```java
package com.yugabyte.example.ycqldataaccess;

public class Customer {

  private long id;
  private String firstName, lastName;

  public Customer(long id, String firstName, String lastName) {
    this.id = id;
    this.firstName = firstName;
    this.lastName = lastName;
  }

  @Override
  public String toString() {
    return String.format(
    "Customer[id=%d, firstName='%s', lastName='%s']",
    id, firstName, lastName);
  }

 // define getters and setters
 // ...
}
```

#### Store and Retrieve Data

Spring Data Cassandra Reactive provides the `ReactiveCassandraRepositories` interface that removes all the boilerplate code and simplifies definition of CRUD operations against YugabyteDB tables. Most of the YugabyteDB connection handling, exception handling, and general error handling is performed by repositories, leaving you to implement the business logic.

Create the `CustomerReactiveRepository` interface, as follows:

```java
package com.yugabyte.example.ycqldataaccess;

import org.springframework.data.cassandra.repository.ReactiveCassandraRepository;
import reactor.core.publisher.Mono;

public interface CustomerReactiveRepository extends
                 ReactiveCassandraRepository<Customer, String> {
  Mono<Customer> findByFirstName(String firstName);
}
```

The following `YCQLReactiveDataAccessApplication.java` is a demonstration of a class that can store and retrieve data from YugabyteDB tables using Cassandra repositories:

```java
package com.yugabyte.example.ycqldataaccess;

import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.data.cassandra.repository.config.EnableReactiveCassandraRepositories;
import com.datastax.oss.driver.api.core.CqlSession;

@SpringBootApplication
@EnableReactiveCassandraRepositories(
  basePackageClasses = CustomerReactiveRepository.class)
public class YcqlReactiveDataAccessApplication implements CommandLineRunner {
  private static final Logger log = LoggerFactory.getLogger(YcqlReactiveDataAccessApplication.class);

  @Autowired
  CustomerReactiveRepository customerReactiveRepository;

  @Autowired
  CqlSession ycqlSession;

  public static void main(String[] args) {
    SpringApplication.run(YcqlReactiveDataAccessApplication.class, args);
  }

  @Override
  public void run(String... args) throws Exception {
    log.info("Creating table");
    ycqlSession.execute("CREATE TABLE IF NOT EXISTS demo.customer (\n" +
                        " id INT PRIMARY KEY,\n"+ " firstName text,\n" +
                        " lastName text\n" + ") WITH default_time_to_live = 0\n" +
                        " AND transactions = {'enabled': 'true'};");

    // Split the array of whole names into an array of first/last names
    List<Object[]> splitUpNames =
      Arrays.asList("Tim Bun", "Mike Dean", "Alan Row", "Josh Rambo")
                    .stream().map(name -> name.split(" "))
                    .collect(Collectors.toList());

    // Use a Java 8 stream to print out each tuple of the list
    splitUpNames.forEach(name -> {
            int id = 1;
            log.info(String.format("Inserting customer record for %s %s",
                                   name[0], name[1]));
            customerReactiveRepository.save(new Customer(id, (String) name[0],
                                                             (String) name[1]));
            id++;
     });

     log.info("Querying for customer records where first_name = 'Josh':");
     // Using Reactive Repository for Querying the data.
     customerReactiveRepository.findByFirstName("Josh")
                               .doOnNext(customer -> log.info(customer.toString()))
                               .block();
  }
}
```

## Compatibility Matrix

| YugabyteDB Java Driver | Spring Data Cassandra |
| :--------------------- | :-------------------- |
| 3.8.0-yb-6 | 2.2.12.RELEASE and later |
| 4.6.0-yb-6 | 3.0.6.RELEASE and later |
