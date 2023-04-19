---
title: Spring Data YugabyteDB
linkTitle: Spring Data YugabyteDB
description: Spring Data YugabyteDB
aliases:
menu:
  preview_integrations:
    identifier: sdyb
    parent: spring-framework
    weight: 578
type: docs
---

Spring Data modules are used for accessing databases and performing various tasks via Java APIs, therefore eliminating the need to learn a database-specific query language.

Spring Data YugabyteDB (SDYB) modules provide support for YSQL APIs and enable you to build cloud-native applications.

{{< note title="Note" >}}

SDYB requires Spring Boot v2.6.0 or earlier. Refer to [Compatibility matrix](#compatibility-matrix).

{{< /note >}}

## Overview

The following are some of the features included in Spring Data YugabyteDB (YSQL):

- Spring Data `YsqlTemplate` and `YsqlRepository` for querying YugabyteDB
- `@EnableYsqlRepostiory` annotation for enabling `YsqlRepository`
- Yugabyte Distributed SQL transaction manager
- Spring Boot starter support for YugabyteDB
- Cluster awareness achieved via elimination of load balancer from SQL
- Topology awareness for developing geographically-distributed applications
- Partition awareness for achieving row-level geo-partitioning

For more information, demonstrations, and contribution guidelines, see [Spring Data YugabyteDB GitHub project.](https://github.com/yugabyte/spring-data-yugabytedb/)

## Project dependencies

The project definition includes the following dependencies:

```xml
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>spring-data-yugabytedb-ysql</artifactId>
    <version>2.3.0-RC1.b5</version>
</dependency>
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>jdbc-yugabytedb</artifactId>
  <version>42.2.7-yb-5.beta.1</version>
</dependency>
```

## Examples

### Spring Data YugabyteDB application

The following tutorial illustrates how to build a Java application with Spring Data YugabyteDB and use the YSQL API to connect to and interact with YugabyteDB. The application connects to a YugabyteDB cluster and performs basic SQL operations, including creating an employees table, adding an employee, and querying the database.

#### Prerequisites

This tutorial assumes that:

- YugabyteDB is up and running. Download and install YugabyteDB by following the steps in [Quick start](../../../quick-start/).
- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later, is installed.

#### Create a project using Spring Boot

The Spring Boot project provides the [Spring Initializr](https://start.spring.io/) utility for generating dependencies for Spring Boot applications.

1. Navigate to [Spring Initializr](https://start.spring.io/). This service pulls in all the dependencies you need for an application and does most of the setup for you.

1. Choose Maven and Java programming language.

1. Click **Dependencies** and select `Spring Web` and `PostgreSQL Driver`. Click **Generate**.

1. Download the resulting ZIP file, which is an archive of a Spring Boot application that is configured with your choices.

1. Add the following dependencies to `pom.xml` of the Spring Boot application:

    ```xml
    <dependency>
      <groupId>com.yugabyte</groupId>
      <artifactId>spring-data-yugabytedb-ysql</artifactId>
      <version>2.3.0</version>
    </dependency>
    <dependency>
      <groupId>com.zaxxer</groupId>
      <artifactId>HikariCP</artifactId>
    </dependency>

    ```

#### Create the sample Spring Data YugabyteDB application

1. Create new files `Employee.java`, `EmployeeRepository.java`, and `YsqlConfig.java` in the base package.

    ```java
    package com.yugabyte.sdyb.sample;

    import org.springframework.data.annotation.Id;
    import org.springframework.data.annotation.Transient;
    import org.springframework.data.domain.Persistable;
    import org.springframework.data.relational.core.mapping.Table;

    @Table(value = "employee")
    public class Employee implements Persistable<String> {

    @Id
    private String id;
    private String name;
    private String email;

    @Transient
    private Boolean isInsert = true;

    // Add Empty Constructor, Constructor, and Getters/Setters

    public Employee() {}

    public Employee(String id, String name, String email) {
      super();
      this.id = id;
      this.name = name;
      this.email = email;
    }

    @Override
    public String getId() {
      return id;
    }

    public void setId(String id) {
      this.id = id;
    }

    public String getName() {
      return name;
    }

    public void setName(String name) {
      this.name = name;
    }

    public String getEmail() {
      return email;
    }

    public void setEmail(String email) {
      this.email = email;
    }

    @Override
    public boolean isNew() {
      return isInsert;
    }

    }
    ```

    ```java
    package com.yugabyte.sdyb.sample;

    import org.springframework.stereotype.Repository;

    import com.yugabyte.data.jdbc.repository.YsqlRepository;

    @Repository
    public interface EmployeeRepository extends YsqlRepository<Employee, Integer> {

    Employee findByEmail(final String email);

    }
    ```

    ```java
    package com.yugabyte.sdyb.sample;

    import javax.sql.DataSource;

    import org.springframework.context.annotation.Bean;
    import org.springframework.context.annotation.Configuration;
    import org.springframework.jdbc.core.namedparam.NamedParameterJdbcOperations;
    import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;
    import org.springframework.transaction.TransactionManager;

    import com.yugabyte.data.jdbc.datasource.YugabyteTransactionManager;
    import com.yugabyte.data.jdbc.repository.config.AbstractYugabyteJdbcConfiguration;
    import com.yugabyte.data.jdbc.repository.config.EnableYsqlRepositories;

    @Configuration
    @EnableYsqlRepositories(basePackageClasses = EmployeeRepository.class)
    public class YsqlConfig extends AbstractYugabyteJdbcConfiguration {

        @Bean
        NamedParameterJdbcOperations namedParameterJdbcOperations(DataSource dataSource) {
            return new NamedParameterJdbcTemplate(dataSource);
        }

        @Bean
        TransactionManager transactionManager(DataSource dataSource) {
            return new YugabyteTransactionManager(dataSource);
        }

    }
    ```

1. A number of options can be customized in the properties file located at `src/main/resources/application.properties`. Given YSQL's compatibility with the PostgreSQL language, the `spring.jpa.database` property is set to POSTGRESQL and the `spring.datasource.url` is set to the YSQL JDBC URL: jdbc:postgresql://localhost:5433/yugabyte.

    ```java
    # ---------------------
    # JPA/Hibernate config.
    spring.jpa.database=POSTGRESQL

    # The SQL dialect makes Hibernate generate better SQL for the chosen database.
    spring.jpa.properties.hibernate.dialect = org.hibernate.dialect.PostgreSQLDialect

    # Hibernate ddl auto (create, create-drop, validate, update).
    spring.jpa.show-sql=true
    spring.jpa.generate-ddl=true
    spring.jpa.hibernate.ddl-auto=create

    # -------------------
    # Data-source config.
    spring.sql.init.platform=postgres
    spring.datasource.url=jdbc:postgresql://localhost:5433/yugabyte
    spring.datasource.username=yugabyte
    spring.datasource.password=

    # HikariCP config (pool size, default isolation level).
    spring.datasource.type=com.zaxxer.hikari.HikariDataSource
    spring.datasource.hikari.transactionIsolation=TRANSACTION_SERIALIZABLE
    ```

1. Create a Spring Boot Application runner to perform reads and writes against the YugabyteDB Cluster.

    ```java
    package com.yugabyte.sdyb.sample;

    import org.springframework.beans.factory.annotation.Autowired;
    import org.springframework.boot.CommandLineRunner;
    import org.springframework.boot.SpringApplication;
    import org.springframework.boot.autoconfigure.SpringBootApplication;
    import org.springframework.jdbc.core.JdbcTemplate;

    @SpringBootApplication
    public class DemoApplication implements CommandLineRunner {

    @Autowired
    JdbcTemplate jdbcTemplate;

    @Autowired
    EmployeeRepository customerRepository;

    public static void main(String[] args) {
      SpringApplication.run(DemoApplication.class, args);
    }

    @Override
    public void run(String... args) throws Exception {

      System.out.println("Connected to the YugabyteDB server successfully.");
      jdbcTemplate.execute("DROP TABLE IF EXISTS employee");
      jdbcTemplate.execute("CREATE TABLE IF NOT EXISTS employee" +
                  "  (id text primary key, name varchar, email varchar)");
      System.out.println("Created table employee");


      Employee customer = new Employee("sl1",
                    "User One",
                    "user@one.com");

      customerRepository.save(customer);

      Employee customerFromDB = null;
      customerFromDB = customerRepository.findByEmail("user@one.com");

      System.out.println(String.format("Query returned: name = %s, email = %s",
        customerFromDB.getName(), customerFromDB.getEmail()));

      }
    }
    ```

#### Run your program

Run your program using the following command:

```sh
$ ./mvnw spring-boot:run
```

You should see the following output:

```output
2022-04-07 20:25:01.210  INFO 12097 --- [main] com.yugabyte.sdyb.demo.DemoApplication   : Started DemoApplication in 27.09 seconds (JVM running for 27.35)
Connected to the YugabyteDB server successfully.
Created table employee
Query returned: name = User One, email = user@one.com
```

### Shopping cart application

The following example demonstrates how to create a basic shopping cart using Spring data YugabyteDB:

```java
public interface ShoppingCartRepository extends YsqlRepository<ShoppingCart, String> {

  ShoppingCart findById(String id);
  List<ShoppingCart> findByUserId(String userId);

}

@Service
public class CartService {

  private final ShoppingCartRepository repository;

  public CartService(CartService repository) {
    this.repository = repository;
  }

  public void doWork() {
    repository.deleteAll();

    ShoppingCart myShoppingCart = new ShoppingCart();
    myShoppingCart.set("cart1")
    myShoppingCart.setUserId("u1001");
    myShoppingCart.setProductId("asin1001");
    myShoppingCart.setQuantity(1);

    repository.save(myShoppingCart);

    ShoppingCart savedCart = repository.findById("cart1");

    List<ShoppingCart> productsInCart = repository.findByUserId("u1001");
    repository.count();
  }

}

@Configuration
@EnableYsqlRepositories
public class YsqlConfig extends AbstractYugabyteJdbcConfiguration {

  @Bean
  DataSource dataSource() {
    String hostName = "127.0.0.1";
    String port = "5433";

    Properties poolProperties = new Properties();
    poolProperties.setProperty("dataSourceClassName",
    "com.yugabyte.ysql.YBClusterAwareDataSource");
    poolProperties.setProperty("dataSource.serverName", hostName);
    poolProperties.setProperty("dataSource.portNumber", port);
    poolProperties.setProperty("dataSource.user", "yugabyte");
    poolProperties.setProperty("dataSource.password", "");
    poolProperties.setProperty("dataSource.loadBalance", "true");
    poolProperties.setProperty("dataSource.additionalEndpoints",
    "127.0.0.2:5433,127.0.0.3:5433");

    HikariConfig hikariConfig = new HikariConfig(poolProperties);
    DataSource ybClusterAwareDataSource = new HikariDataSource(hikariConfig);
    return ybClusterAwareDataSource;
  }

  @Bean
  JdbcTemplate jdbcTemplate(@Autowired DataSource dataSource) {

    JdbcTemplate jdbcTemplate = new JdbcTemplate(dataSource);
    return jdbcTemplate;
  }

  @Bean
  NamedParameterJdbcOperations namedParameterJdbcOperations(DataSource dataSource) {
    return new NamedParameterJdbcTemplate(dataSource);
  }

  @Bean
  TransactionManager transactionManager(DataSource dataSource) {
    return new YugabyteTransactionManager(dataSource);
  }

}
```

For additional information and examples, refer to [SDYB example](https://github.com/yugabyte/spring-data-yugabytedb-example).

## Compatibility matrix

| SDYB | YugabyteDB Smart Driver | PostgreSQL JDBC Driver | Spring Boot |
| :--- | :---------------------- | :--------------------- | :---------- |
| 2.3.0 | 42.3.4 | 42.2.7 or later | 2.6 or earlier |
