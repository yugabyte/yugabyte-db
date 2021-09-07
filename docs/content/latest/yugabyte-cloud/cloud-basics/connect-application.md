---
title: Connect a Spring Data YugabyteDB application
linkTitle: Connect a Spring application
description: Connect a Spring Spring Data YugabyteDB application to Yugabyte Cloud.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: connect-application
    parent: cloud-basics
    weight: 80
isTocNested: true
showAsideToc: true
---

The example on this page shows how you can connect a Spring application to Yugabyte Cloud, using a version of the Spring PetClinic sample application that has been updated with a domain and persistence layer built with [Spring Data YugabyteDB](https://github.com/yugabyte/spring-data-yugabytedb).

In addition to using Spring Data YugabyteDB, the application uses the following:

- [Flyway](https://flywaydb.org/) to manage the database schema and initial data loading.
- [SpringFox](https://springfox.github.io/springfox/) to deliver Swagger/OpenAPI docs.

The repository for the application is at <https://github.com/yugabyte/petclinic-spring-data-yugabytedb>. Instructions for connecting this application to YugabyteDB are also provided in the `readme.md` file in this repository.

In this walkthrough, you will:

- Download the Spring PetClinic application
- Obtain your cluster connection string
- Update and run the application locally

## Prerequisites

- Git
- Yugabyte Cloud cluster, with your computer IP address whitelisted in an [IP allow list](../../cloud-basics/add-connections/)

## Clone the Spring Data YugabyteDB PetClinic application

1. On your computer, clone the Spring PetClinic sample application built with Spring Data YugabyteDB: 

    ```sh
    $ git clone https://github.com/yugabyte/petclinic-spring-data-yugabytedb.git
    ```

    ```output
    Cloning into 'petclinic-spring-data-yugabytedb'...
    remote: Enumerating objects: 177, done.
    remote: Counting objects: 100% (177/177), done.
    remote: Compressing objects: 100% (121/121), done.
    remote: Total 177 (delta 43), reused 171 (delta 37), pack-reused 0
    Receiving objects: 100% (177/177), 1.02 MiB | 1.28 MiB/s, done.
    Resolving deltas: 100% (43/43), done.
    ```

1. Go to the `petclinic-spring-data-yugabytedb` directory.

    ```sh
    $ cd petclinic-spring-data-yugabytedb
    ```

## Install the certificate and copy the connection string

1. Sign in to Yugabyte Cloud, select your cluster, and click **Connect**.

1. Click **Connect to your Application**.

1. Click **Download CA Cert** to download the cluster `root.crt` certificate to your computer, and move the file to the `~/.postgresql` directory.

    ```sh
    $ mv ~/Downloads/root.crt ~/.postgresql/root.crt
    ```

1. Under **Add this connection string into your application**, copy the postgresql connection string.

    The connection string is in the form `postgresql://[host]:[port]/[database]`.

## Connect and run the application

1. On your computer, update the contents of the `spring-petclinic/src/main/resources/application.yml` file by updating the `yugabyte:datasource:url` with the connection string you copied, and username and password with your YugabyteDB database credentials. 

    The connection string replaces the url after `jdbc:` and before `?ssl`, as follows:

    ```yaml
    yugabyte:
    datasource:
        url: jdbc:[postgresql connection string]?ssl=true&sslmode=verify-full
        load-balance: true
        username: [user]
        password: [password]
    ```

    where `[postgresql connection string]` is the connection string, and `[user]` and `[password]` are the credentials for the database.

1. Run the PetClinic application using the following command:

    ```sh
    $ ./mvnw spring-boot:run
    ```

    ```output
    [INFO] Scanning for projects...
    [INFO] 
    [INFO] ----< org.springframework.samples:petclinic-spring-data-yugabytedb >----
    [INFO] Building petclinic 2.1.0.BUILD-SNAPSHOT
    [INFO] --------------------------------[ jar ]---------------------------------
    [INFO] 
    [INFO] >>> spring-boot-maven-plugin:2.5.4:run (default-cli) > test-compile @ petclinic-spring-data-yugabytedb >>>
    ```

    Flyway configures the Yugabyte database with the Petclinic schema and loads the sample data.

1. Go to <http://localhost:8080>.

The PetClinic application is now running locally and is connected to your Yugabyte Cloud cluster.

![PetClinic application running](/images/yb-cloud/petclinic-springdata.png)
