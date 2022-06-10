---
title: Spring Data YugabyteDB
linkTitle: Spring Data YugabyteDB
description: Spring Data YugabyteDB
aliases:
menu:
  preview:
    identifier: sdyb
    parent: spring-framework
    weight: 578
type: docs
---

Spring Data modules are used for accessing databases and performing various tasks via Java APIs, therefore eliminating the need to learn a database-specific query language.

Spring Data YugabyteDB (SDYB) modules provide support for YSQL APIs and enable you to build cloud-native applications.

## Overview

The following are some of the features included in SDYB YSQL:

- Spring Data `YsqlTemplate` and `YsqlRepository` for querying YugabyteDB
- `@EnableYsqlRepostiory` annotation for enabling `YsqlRepository`
- Yugabyte Distributed SQL transaction manager
- Spring Boot starter support for YugabyteDB
- Cluster awareness achieved via elimination of load balancer from SQL
- Topology awareness necessary for developing geographically-distributed applications
- Partition awareness for achieving row-level geo-partitioning

For more information, demonstrations, and contribution guidelines, see [Spring Data YugabyteDB GitHub project.](https://github.com/yugabyte/spring-data-yugabytedb/)

## Project Dependencies

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


The following example demonstrates how to create a basic shopping cart using SDYB:

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

For additional information and examples, see [ SDYB example](https://github.com/yugabyte/spring-data-yugabytedb-example).

## Compatibility Matrix

| **SDYB** | **YugabyteDB Smart Driver** | **PostgreSQL JDBC Driver** |
| -------- | --------------------------- | ------------------------ |
| 2.3.0    | 42.3.4          | 42.2.7 or later
