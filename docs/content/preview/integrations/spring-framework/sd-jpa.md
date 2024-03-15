---
title: Spring Data JPA
linkTitle: Spring Data JPA
description: Spring Data JPA
aliases:
menu:
  preview_integrations:
    identifier: spring-JPA
    parent: spring-framework
    weight: 579
type: docs
---

[Spring Data JPA](https://spring.io/projects/spring-data-jpa) is a popular Spring Framework projects, and simplifies creating JPA-based repositories by reducing the boilerplate code required to implement data access layers. It provides enhanced support for JPA-based data access using Spring annotation driven programming.

Spring Data JPA provides out-of-the-box support for PostgreSQL-compliant databases. This includes auto-reconfiguration support for creating the data source objects based on the database driver in the classpath. YugabyteDB provides complete support for Spring Data JPA using the PostgreSQL dialect. Spring Data JPA repositories can be used to read and write data to YugabyteDB clusters.

This document describes the fundamentals of using Spring Data JPA with YugabyteDB to create Spring Boot applications.

## Quick start

Learn how to establish a connection to YugabyteDB database and begin basic CRUD operations using the steps in [Java ORM example application](../../../drivers-orms/orms/java/ysql-spring-data/).

## Project dependencies

Spring Data JPA can be used with both YugabyteDB JDBC driver and the upstream PostgreSQL JDBC driver.

### Maven dependency

Add the following dependencies for Spring Data JPA with [YugabyteDB JDBC Driver](../../../drivers-orms/java/yugabyte-jdbc/).

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

If you're planning to use the [PostgreSQL JDBC driver](/preview/drivers-orms/java/postgres-jdbc/), use the following dependencies:

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

Learn how to perform common tasks required for developing Spring Boot applications using Spring Data JPA with YugabyteDB YSQL API.

### Connect to YugabyteDB Database using Spring Data JPA

The Spring Data JPA project provides number of `spring.datasource.*` properties that can be customized to configure the data source and transaction behavior of Spring Boot applications.

Configure the Spring Boot application to connect to a YugabyteDB Cluster using the following properties in the `application.properties` file:

#### YugabyteDB JDBC driver

| spring property | Description | Value |
| :---------- | :---------- | :------ |
| spring.datasource.url  | JDBC URL | jdbc:yugabytedb://127.0.0.1:5433/yugabyte?load-balance=true
| spring.datasource.username | user for connecting to the database | yugabyte
| spring.datasource.password | password for connecting to the database | yugabyte
| spring.datasource.driver-class-name | JDBC driver classname | com.yugabyte.Driver
| spring.datasource.hikari.minimum-idle | JDBC connection idle timeout | 5
| spring.datasource.hikari.maximum-pool-size | Maximum number of DB connections in the Pool | 20
| spring.datasource.hikari.auto-commit | Transaction commit behavior | false

#### PostgreSQL JDBC driver

| spring property | Description | Value |
| :---------- | :---------- | :------ |
| spring.datasource.url  | JDBC URL | jdbc:postgresql://localhost:5433/yugabyte
| spring.datasource.username | user for connecting to the database | yugabyte
| spring.datasource.password | password for connecting to the database | yugabyte
| spring.datasource.hikari.minimum-idle | JDBC connection idle timeout | 5
| spring.datasource.hikari.maximum-pool-size | Maximum number of DB connections in the Pool | 20
| spring.datasource.hikari.auto-commit | Transaction commit behavior | false

### Create table

Spring Data JPA uses [Hibernate ORM](/preview/drivers-orms/java/hibernate/) for handling creation of the database tables based on the domain objects configured in the Spring Boot application. Java POJOs annotated with `@Table` and `@Entity` are mapped to the corresponding YugabyteDB table.

For example:

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

This domain object maps to the `users` table on the database, which has `userId` as the primary key and an auto incrementing type.

Spring Boot applications can be configured to determine whether tables should be created on application startup or not. Add the following property in `application.properties` to configure the database table creation behavior.

```xml
# Hibernate ddl auto (create, create-drop, validate, update, none).
spring.jpa.hibernate.ddl-auto=create
```

For production environments, it's good practice to disable database table creation on startup. To prevent table creation on startup, set the property to `none`.

### Read and write data

#### Enable JPA repositories

Spring Data JPA repositories provide support for `CRUDRepository`, `JPARepository`, and `PagingAndSortingRepository`. By enabling these repositories, Spring Boot applications get access to data access methods out of the box. For more information, refer to the [Spring Data JPA reference](https://docs.spring.io/spring-data/jpa/docs/current/reference/html/#jpa.query-methods.query-creation) documentation.

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

Spring Boot applications can be configured to enable these repositories by using the following Java configuration class.

```java
import org.springframework.data.jpa.repository.config.EnableJpaRepositories;

@EnableJpaRepositories
class Config { … }
```

#### Insert data

To write data to YugabyteDB, implement the repository for each domain object.

For example:

```java
import org.springframework.data.jpa.repository.JpaRepository;
import com.yugabyte.springdemo.model.User;

public interface UserRepository extends JpaRepository<User, Long> {

}
```

This repository can then be used to insert data into the `Users` table.

```java

@Autowired
private UserRepository userRepository;

...

@PostMapping("/users")
public User createUser(@Valid @RequestBody User user) {
    return userRepository.save(user);
}
```

#### Query data

To read data in YugabyteDB, use the out-of-the-box query methods exposed by the repositories.

For example:

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

Spring Data JPA repositories provide [named queries](https://docs.spring.io/spring-data/jpa/reference/jpa/query-methods.html) for adding custom queries to repositories and to update the behavior of built-in queries by using the `@Query` annotation in the repository implementation.

For example:

```java
import org.springframework.data.jpa.repository.JpaRepository;
import com.yugabyte.springdemo.model.User;

public interface UserRepository extends JpaRepository<User, Long> {

  @Query("select u from User u where u.emailAddress = ?1")
  User findByEmailAddress(String emailAddress);
}
```

### Transaction and isolation levels

YugabyteDB supports transactions for inserting and querying data from the tables. YugabyteDB supports different [isolation levels](../../../../architecture/transactions/isolation-levels/) for maintaining strong consistency for concurrent data access.

By default, CRUD methods on a JPA repository are transactional. For read operations, the transaction configuration `readOnly` flag is set to true. All others are configured with a plain `@Transactional` so that the default transaction configuration applies.

The transaction and isolation level of Spring Boot applications can be configured by setting the following properties in the `application.properties` file.

```java
spring.datasource.hikari.transactionIsolation=TRANSACTION_SERIALIZABLE
```

## Compatibility matrix

| **Spring Data JPA** | **YugabyteDB Smart Driver** | **Postgres JDBC Driver** |
| -------- | --------------------------- | ------------------------ |
| 2.6.x    | 42.3.4          | 42.2.7 or later
| 2.5.x    | 42.3.4          | 42.2.7 or later
