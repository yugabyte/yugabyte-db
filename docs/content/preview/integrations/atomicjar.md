---
title: AtomicJar Testcontainers
linkTitle: AtomicJar Testcontainers
description: Use AtomicJar Testcontainers with YSQL API
menu:
  preview_integrations:
    identifier: ysql-atomicjar
    parent: application-frameworks
    weight: 571
type: docs
---

[AtomicJar Testcontainers](https://www.testcontainers.org/) is a Java library that supports JUnit tests, providing lightweight, throwaway instances of common services that can run in a Docker container. Though initially targeted at the JVM ecosystem, it can be leveraged for other programming languages. Testcontainers is a great way to write integration tests for frameworks like Spring Boot, with a real database.

YugabyteDB supports for the [YugabyteDB module](https://www.testcontainers.org/modules/databases/yugabytedb/) in Testcontainers.

This document describes how to use Testcontainers to write integration tests for a Spring Boot application with YugabyteDB .

## Prerequisites

To use Testcontainers, ensure that you have the following:

- Java Development Kit (JDK) 8 or later installed. You can use [SDKMAN](https://sdkman.io/install) to install the JDK runtime.

- Docker Engine. To download and install Docker, select one of the following environments:

    <i class="fa-brands fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)

    <i class="fa-brands fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos)

    <i class="fa-brands fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)

    <i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)

## Build the Testcontainers project

Refer to the [Spring Boot source](https://github.com/yugabyte/yb-ms-data/tree/main/springboot) from GitHub for your local setup. The source consists of a basic JPA-based web application with CRUD functionality.

You need to build the Testcontainers project using the following steps:

1. Define the library dependency in the build file. Refer to the `build.gradle` file for complete details.

    ```properties

    // version details for the BOM
    extra["testcontainersVersion"] = "1.17.6"

    // library
    dependencies {
        testImplementation("org.testcontainers:yugabytedb")
        testImplementation("org.testcontainers:junit-jupiter")
    }

    dependencyManagement {
    imports {
        mavenBom("org.testcontainers:testcontainers-bom:${property("testcontainersVersion")}")
    }
    }
    ```

1. Initialize the YugabyteDB Testcontainers using the `@Container` annotation.

    ```properties
    @Container
    YugabyteDBYSQLContainer container = new YugabyteDBYSQLContainer("yugabytedb/yugabyte:2.16.0.0-b90").withDatabaseName("yugabyte").withUsername("yugabyte").withPassword("yugabyte").withReuse(true);
    ```

   Refer to `AbstractTodoApplicationTest.java` class file for the complete definition.

   The following snippet is required to populate the data source information:

   ```properties
   @DynamicPropertySource
   static void datasourceProps(final DynamicPropertyRegistry registry) {
     registry.add("spring.datasource.url", container::getJdbcUrl);
     registry.add("spring.datasource.username", container::getUsername);
     registry.add("spring.datasource.password", container::getPassword);
     registry.add("spring.datasource.driver-class-name", () -> "com.yugabyte.Driver");
   }
   ```

    An alternative to the preceding step is to define the `tc` JDBC URL in the `application-test.yaml` properties file.

    ```properties
    spring:
      datasource:
        url: jdbc:tc:yugabyte:2.14.4.0-b26:///yugabyte
        username: yugabyte
        password: yugabyte
    ```

## Write the integration test classes

This example uses the standard way of writing integration test cases for the Spring Boot test framework with the following annotations:

- `@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)` - This annotation loads the application context. The `webEnvironment` attribute is set to NONE because you do not want to start the embedded servlet container for your test. This property is mainly used when you want to test the service or repository layer.

- `@AutoConfigureTestDatabase(replace = NONE)` - This annotation replaces any data source with the embedded H2 instance by default. So, you need to override this behavior by adding `replace=Replace.NONE` so that the application uses the real database through Testcontainers.

- `@Testcontainers` - This annotation enables Testcontainers for the test class.

- `@ActiveProfiles(“test”)` - This annotation activates the test application profile (application-test.[yaml|properties]). This profile configures the application to use the real database through Testcontainers.

- `@DataJpaTest`- This annotation is for a JPA test focusing only on the repository components.

Refer to `TodoApplicationServiceTest.java` and `TodoApplicationRepositoryTest.java` to learn how to test the service layer and repository layer respectively.

## Learn more

- [YugabyteDB module for Testcontainers](https://www.testcontainers.org/modules/databases/yugabytedb/)
- [Testing Spring Boot Applications with YugabyteDB Using Testcontainers](https://www.yugabyte.com/blog/use-testcontainers-test-spring-boot-applications/) for additional information on trying Testcontainers using Gitpod.
