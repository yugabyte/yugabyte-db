<!--
title: Connect a Spring application
linkTitle: Connect a Spring application
description: Connect a Spring application to Yugabyte Cloud and containerize it in Docker.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: spring-boot-app
    parent: spring-boot
    weight: 10
isTocNested: true
showAsideToc: true
-->

[Spring Boot](https://spring.io/projects/spring-boot) is a popular framework for building cloud native applications. Each Spring Boot application is stand-alone and self-contained, which makes them easy to deploy in a distributed fashion – whether to containers or on Kubernetes.

The example on this page shows how you can connect a Spring Boot application to Yugabyte Cloud, using a version of the Spring Boot PetClinic sample application that has been updated with a profile making it compatible with YugabyteDB. 

The repository for the application is at <https://github.com/yugabyte/spring-petclinic>. Instructions for connecting this application to YugabyteDB are also provided in the `petclinic_db_setup_yugabytedb.md` file in this repository.

In this walkthrough, you will:

- Download the Spring Boot PetClinic application
- Connect the application to Yugabyte Cloud
- Containerize the application in Docker

## Prerequisites

- Java 8 or newer (full JDK)
- Git
- Docker
- Yugabyte Cloud cluster, with your computer IP address [whitelisted in an IP allow list](../cloud-secure-clusters/add-connections/)

## Download and connect the PetClinic application

1. On your computer, clone the Spring Boot PetClinic sample application: 

    ```sh
    $ git clone https://github.com/yugabyte/spring-petclinic.git
    ```

    ```output
    Cloning into 'spring-petclinic'...
    remote: Enumerating objects: 8616, done.
    remote: Counting objects: 100% (18/18), done.
    remote: Compressing objects: 100% (18/18), done.
    remote: Total 8616 (delta 1), reused 13 (delta 0), pack-reused 8598
    Receiving objects: 100% (8616/8616), 7.29 MiB | 19.03 MiB/s, done.
    Resolving deltas: 100% (3268/3268), done.
    ```

1. Go to the `spring-petclinic` directory.

    ```sh
    $ cd spring-petclinic
    ```

1. Open the file `spring-petclinic/src/main/resources/db/yugabytedb/user.sql` and copy the contents.

1. Sign in to Yugabyte Cloud and select your cluster, and note the host and port details.

1. Click **Connect** and choose **Launch Cloud Shell**.

1. Paste the contents of the `user.sql` file into the Cloud Shell to create the application database and user.

    ```sql
    yugabyte=# DROP DATABASE IF EXISTS "petclinic";
    yugabyte=# DROP USER IF EXISTS "petclinic";
    yugabyte=# CREATE DATABASE "petclinic";
    yugabyte=# CREATE USER "petclinic" WITH PASSWORD 'petclinic';
    yugabyte=# GRANT ALL PRIVILEGES ON DATABASE "petclinic" to "petclinic";
    ```

    ```output
    NOTICE:  database "petclinic" does not exist, skipping
    DROP DATABASE
    NOTICE:  role "petclinic" does not exist, skipping
    DROP ROLE
    CREATE DATABASE
    CREATE ROLE
    GRANT
    ```

1. On your computer, run the PetClinic application using the following command:

    ```sh
    $ ./mvnw spring-boot:run \
    -Dspring-boot.run.profiles=yugabytedb \
    -Dspring-boot.run.arguments="--YBDB_URL=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true"
    ```

    where `[host]` and `[port]` are the host and port number of your Yugabyte Cloud cluster. The `spring-boot.run.profiles` parameter tells the application to use the YugabyteDB database configuration. The `spring-boot.run.arguments` parameter provides the application with the connection string to your Yugabyte Cloud cluster.

    ```output
    [INFO] Scanning for projects...
    [INFO] 
    [INFO] ------------< org.springframework.samples:spring-petclinic >------------
    [INFO] Building petclinic 2.4.5
    [INFO] --------------------------------[ jar ]---------------------------------
    [INFO] 
    [INFO] >>> spring-boot-maven-plugin:2.4.5:run (default-cli) > test-compile @ spring-petclinic >>>
    ```

    {{< note title="Note" >}}

This configuration automatically initializes your database, and will delete and reinitialize the database on every run. To not initialize the database after the first run, use the `--DB_INIT=never` argument:

```sh
$ ./mvnw spring-boot:run \
Dspring-boot.run.profiles=yugabytedb \
-Dspring-boot.run.arguments="--YBDB_URL=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true, \
--DB_INIT=never"
```

    {{< /note >}}

1. Go to <http://localhost:8080>.

The PetClinic application is now running locally and is connected to your Yugabyte Cloud cluster.

![PetClinic application running](/images/yb-cloud/petclinic.png)

## Containerize the application using Docker and run locally

1. Start Docker on your computer.

1. Containerize the PetClinic application: 

    ```sh
    $ ./mvnw spring-boot:build-image
    ```

1. Tag your image: 

    ```sh
    $ docker tag [image_id] spring-petclinic
    ```
    
    You can find the image id by running `docker image ls`.

1. Run the image as a container in Docker to make sure it’s working correctly: 

    ```sh
    $ docker run -d --name=spring-petclinic -p 8080:8080 -e \
    JAVA_OPTS="-Dspring.profiles.active=yugabytedb \
    -Dspring.datasource.url=jdbc:postgresql://[host]:[port]/petclinic?load-balance=true \
    -Dspring.datasource.initialization-mode=never" spring-petclinic
    ```

    where `[host]` and `[port]` are the host and port number of your Yugabyte Cloud cluster.

1. Go to <http://localhost:8080>.

The PetClinic sample application is now connected to your Yugabyte Cloud cluster and running locally on Docker.
