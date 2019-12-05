---
title: Spring Data YugabyteDB
linkTitle: Spring Data YugabyteDB
description: Spring Data YugabyteDB
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
section: REFERENCE
menu:
  latest:
    identifier: spring-data-yugabytedb
    parent: connectors
    weight: 2940
---


The Spring Data YugabyteDB driver brings the power of distributed SQL to Spring developers by using an enhanced underlying https://github.com/yugabyte/jdbc-yugabytedb[JDBC driver] - which brings most features of PostgreSQL (as of v11.2) into a distributed database that is scalable and fault-tolerant. Spring Data YugabyteDB is based on https://github.com/spring-projects/spring-data-jpa[Spring Data JPA].

## Unique Features

In addition to providing most PostgreSQL features on top of a scalable and resilient distributed database, this project aims to enable the following:

* Eliminate load balancer from SQL (cluster-awareness)
* Develop geo-distributed Apps (topology-awareness)
* Row level geo-partitioning support (partition-awareness)

Go to the [Spring Data YugabyteDB GitHub project](https://github.com/yugabyte/spring-data-yugabytedb/blob/master/readme.adoc) to watch, star, file issues, and contribute.

## Getting Started

### Maven configuration

Add the Maven dependency:

```xml
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>spring-data-yugabytedb</artifactId>
  <version>2.1.10-yb-1</version>
</dependency>
```

### Data source configuration

To enable the YugabyteDB configuration, create configuration class:

```java
@Configuration
public class DatabaseConfiguration extends AbstractYugabyteConfiguration {
  // Here you can override the dataSource() method to configure the DataSource in code.
}
```

Configure your `application.properties`. For instance:

```
spring.yugabyte.initialHost=localhost
spring.yugabyte.port=5433
spring.yugabyte.database=yugabyte
spring.yugabyte.user=yugabyte
spring.yugabyte.password=yugabyte
spring.yugabyte.maxPoolSizePerNode=8
spring.yugabyte.connectionTimeoutMs=10000
spring.yugabyte.generate-ddl=true
spring.yugabyte.packages-to-scan=com.example.spring.jpa.springdatajpaexample.domain
```

To learn more about using the Spring Data YugabyteDB module, see [Spring Data YugabyteDB example](https://github.com/yugabyte/spring-data-yugabytedb-example).
