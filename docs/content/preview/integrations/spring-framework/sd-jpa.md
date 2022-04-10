---
title: Spring Data JPA
linkTitle: Spring Data JPA
description: Spring Data JPA
aliases:
menu:
  preview:
    identifier: spring-JPA
    parent: spring-framework
    weight: 579
isTocNested: true
showAsideToc: true
---

This document describes fundamentals of using Spring Data JPA project with YugabyteDB Database for creating Spring Boot applications.

## Spring Data JPA

[Spring Data JPA](https://spring.io/projects/spring-data-jpa) is one of the popular Spring Framework projects. Spring Data JPA makes it easy to implement JPA based repositories by reducing the boilerplate code required for implementing the data access layers. It provides enhanced support for JPA based data access using Spring annotation driven programming.

Spring Data JPA provides out of the box support for PostgreSQL complaint databases including auto-reconfiguration support for automatically creating the data source objects based on the Database driver on the classpath. YugabyteDB provides complete support for Spring Data JPA using the Postgres Dialect. Spring Data JPA repositories can be used for reading and writing data into YugabyteDB cluster.

### Quick Start

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/java/ysql-spring-data/) in the Quick Start section.

## Project Dependencies

Spring Data JPA can be used with both YugabyteDB JDBC driver and the upstream Postgres JDBC driver.

### Maven Dependency

Add the following dependencies for Spring Data JPA with [YugabyteDB JDBC Driver](/preview/drivers-orms/java/yugabyte-jdbc/).

```xml
<dependencies>
  <dependency>
    <groupId>org.springframework.data</groupId>
    <artifactId>spring-data-jpa</artifactId>
    <version>2.6.3</version>
  </dependency>
  <dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>jdbc-yugabytedb</artifactId>
    <version>42.3.4</version>
  </dependency>
<dependencies>
```

If you're planning to use [PostgreSQL JDBC driver](/preview/drivers-orms/java/postgres-jdbc/), use the following dependencies

```xml
<dependencies>
  <dependency>
    <groupId>org.springframework.data</groupId>
    <artifactId>spring-data-jpa</artifactId>
    <version>2.6.3</version>
  </dependency>
  <dependency>
    <groupId>org.postgresql</groupId>
    <artifactId>postgresql</artifactId>
    <version>42.2.14</version>
  </dependency>
<dependencies>
```

## Fundamentals

Learn how to perform the common tasks required for developing Spring Boot applications using Spring Data JPA with YugabyteDB YSQL API.

### Connect to YugabyteDB Database using Spring Data JPA

Spring Data JPA project provides number of `spring.datasource.*` properties that can be customized to configure the data source and transaction behavior of the Spring Boot applications.

Configure the Spring boot application to connect to YugabyteDB Cluster using the following properties in the application.properties file

#### For YugabyteDB JDBC Driver

| spring property | Description | Value |
| :---------- | :---------- | :------ |
| spring.datasource.url  | JDBC URL | jdbc:yugabytedb://127.0.0.1:5433/yugabyte?load-balance=true
| spring.datasource.username | user for connecting to the database | yugabyte
| spring.datasource.password | password for connecting to the database | yugabyte
| spring.datasource.driver-class-name | JDBC driver classname | com.yugabyte.Driver
| spring.datasource.hikari.minimum-idle | JDBC connection idle timeout | 5
| spring.datasource.hikari.maximum-pool-size | Maximum number of DB connections in the Pool | 20
| spring.datasource.hikari.auto-commit | Transaction commit behavior | false

#### For PostgreSQL JDBC Driver

| spring property | Description | Value |
| :---------- | :---------- | :------ |
| spring.datasource.url  | JDBC URL | jdbc:postgresql://localhost:5433/yugabyte
| spring.datasource.username | user for connecting to the database | yugabyte
| spring.datasource.password | password for connecting to the database | yugabyte
| spring.datasource.hikari.minimum-idle | JDBC connection idle timeout | 5
| spring.datasource.hikari.maximum-pool-size | Maximum number of DB connections in the Pool | 20
| spring.datasource.hikari.auto-commit | Transaction commit behavior | false

### Create table

Spring Data JPA uses [Hibernate ORM](/preview/drivers-orms/java/hibernate/) for handling creation of the database tables based on the domain objects configured in the Spring Boot application. Java POJOs annotated with `@Table` and `@Entity` annotation will be mapped to corresponding YugabyteDB table.

For Example

```java
@Entity
@Table(name = "users")
public class User {

 @Id
 @GeneratedValue(strategy = GenerationType.IDENTITY)
 @Column(columnDefinition = "serial")
 private Long userId;

...
```

Above domain object maps to `users` table on the database which has `userId` as primary key with auto incrementing type.

Also, Spring Boot application can configured to determine the behavior of table creation, whether the tables should be created on app startup or not. Add the following property in `application.properties` for configuring the database table creation behavior,

```xml
# Hibernate ddl auto (create, create-drop, validate, update, none).
spring.jpa.hibernate.ddl-auto=create
```

For production environment, its a good practice to disable the database table creation on application start up. For production deployments, it is recommended to configure the property to `none` to prevent the table creation on startup.

### Read and Write Data

#### Enable JPA Repositories

Spring Data JPA repositories provides support for `CRUDRepository`, `JPARepository`, and `PagingAndSortingRepository`. By enabling these repositories, Spring boot applications get access to data access methods out of the box. For more information refer to the [Spring Data JPA reference](https://docs.spring.io/spring-data/jpa/docs/current/reference/html/#jpa.query-methods.query-creation) documentation.

```java
public interface CrudRepository<T, ID> extends Repository<T, ID> {

  <S extends T> S save(S entity);      

  Optional<T> findById(ID primaryKey); 

  Iterable<T> findAll();               

  long count();                        

  void delete(T entity);               

  boolean existsById(ID primaryKey);   

  // … more functionality omitted.
}
```

Spring boot application can be configured to enable these repositories by using the following Java configuration class.

```java
import org.springframework.data.jpa.repository.config.EnableJpaRepositories;

@EnableJpaRepositories
class Config { … }
```

#### Insert Data

To write data in to YugabyteDB, implement the repository for each domain object.

For example

```java
import org.springframework.data.jpa.repository.JpaRepository;
import com.yugabyte.springdemo.model.User;

public interface UserRepository extends JpaRepository<User, Long> {

}
```

This repository can then be used for inserting data in to `Users` table.

```java

@Autowired
private UserRepository userRepository;

...

@PostMapping("/users")
public User createUser(@Valid @RequestBody User user) {
    return userRepository.save(user);
}
```

#### Query Data

To write data in to YugabyteDB, use the out of the box query methods exposed by the repositories.

For Example

```java
@Autowired
private UserRepository userRepository;

@PutMapping("/users/{userId}")
public User updateUser(@PathVariable Long userId,
                                @Valid @RequestBody User userRequest) {
    return userRepository.findById(userId)
            .map(user -> {
                user.setFirstName(userRequest.getFirstName());
                user.setLastName(userRequest.getLastName());
                user.setEmail(userRequest.getEmail());
                return userRepository.save(user);
            }).orElseThrow(() -> new ResourceNotFoundException("User not found with id " + userId));
}
```

Spring Data JPA repositories provides [named queries](https://docs.spring.io/spring-data/jpa/docs/current/reference/html/#jpa.query-methods.at-query) for adding custom queries to repositories and to update the behavior of in built queries by using `@Query` annotation in the repository implementation.

For Example

```java
import org.springframework.data.jpa.repository.JpaRepository;
import com.yugabyte.springdemo.model.User;

public interface UserRepository extends JpaRepository<User, Long> {

  @Query("select u from User u where u.emailAddress = ?1")
  User findByEmailAddress(String emailAddress);
}
```

### Transaction and Isolation Levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

By default, CRUD methods on JPA repository are transactional. For read operations, the transaction configuration `readOnly` flag is set to true. All others are configured with a plain `@Transactional` so that default transaction configuration applies.

The transaction and isolation level of the Spring Boot applications can be configured by setting the following properties in `application.properties` file

```java
spring.datasource.hikari.transactionIsolation=TRANSACTION_SERIALIZABLE
```

## Compatibility Matrix

| **Spring Data JPA** | **YugabyteDB Smart Driver** | **Postgres JDBC Driver** |
| -------- | --------------------------- | ------------------------ |
| 2.6.x    | 42.3.4          | 42.2.7 or later
| 2.5.x    | 42.3.4          | 42.2.7 or later