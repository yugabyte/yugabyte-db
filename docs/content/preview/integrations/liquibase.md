---
title: Liquibase
linkTitle: Liquibase
description: Using Liquibase with YugabyteDB
menu:
  preview_integrations:
    identifier: liquibase
    parent: schema-migration
    weight: 571
type: docs
---

This document describes how to migrate data using [Liquibase](https://www.liquibase.com/) with YugabyteDB.

## Prerequisites

To use Liquibase with YugabyteDB, you need the following:

- JDK version 8 or later.
- YugabyteDB version 2.6 or later (see [Quick start](../../quick-start/)).
- Liquibase (see [Download Liquibase](https://www.liquibase.org/download)). For information on how to extract the package and configure Liquibase, see [Configuring Liquibase](#configuring-liquibase).
- Liquibase-YugabyteDB extension JAR (access [liquibase-yugabytedb repository](https://github.com/liquibase/liquibase-yugabytedb) and download the latest `liquibase-yugabytedb-.jar`). The driver must be located in the `/lib` sub-directory of the directory to which you extracted Liquibase.
- PostgreSQL JDBC driver (see [PostgreSQL JDBC driver](https://jdbc.postgresql.org)). The driver must be located in the `/lib` sub-directory of the directory to which you extracted Liquibase.

## Configure Liquibase

You configure Liquibase as follows:

- Extract the `.tar` file into a new directory by executing the following command:

  ```shell
  tar -xvf liquibase-<version>.tar.gz
  ```

- Add the Liquibase path as an environment variable on your local computer by executing the following command:

  ```bash
  echo "export PATH=$PATH:/<full-path>/liquibase-<version>" >> ~/.bash_profile

  source ~/.bash_profile
  ```

  If your terminal does not run `.bash_profile` on startup, you can append the Liquibase path to the `PATH` definition in `.bashrc` or `.profile`.

- Verify the installation by executing the following command:

  ```shell
  liquibase --version
  ```

- Create a changelog file called `master-changelog.xml` by executing the following command:

  ```shell
  touch master-changelog.xml
  ```

  Liquibase uses this file to perform changes on the database schema. This file contains a list of instructions (changesets) that are executed against the database. Note that currently, the following changesets are not supported by Yugabyte: `addUniqueConstraint`, `alterSequence`, `dropPrimaryKey`, `dropUniqueConstraint`, `renameSequence`; in addition, Liquibase Pro changesets have not been tested against YugabyteDB.

  Changelog files can be created as `.xml`, `.sql`, `.yaml`, and `.json`.

  Add the following code to the `master-changelog.xml` file:

  ```xml
  <?xml version="1.1" encoding="UTF-8" standalone="no"?>

  <databaseChangeLog xmlns="http://www.liquibase.org/xml/ns/dbchangelog" xmlns:ext="http://www.liquibase.org/xml/ns/dbchangelog-ext" xmlns:pro="http://www.liquibase.org/xml/ns/pro" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:schemaLocation="http://www.liquibase.org/xml/ns/dbchangelog-ext http://www.liquibase.org/xml/ns/dbchangelog/dbchangelog-ext.xsd http://www.liquibase.org/xml/ns/pro http://www.liquibase.org/xml/ns/pro/liquibase-pro-4.1.xsd http://www.liquibase.org/xml/ns/dbchangelog http://www.liquibase.org/xml/ns/dbchangelog/dbchangelog-4.1.xsd">

    <changeSet author="abc" id="1">
      <createTable tableName="actor">
        <column autoIncrement="true" name="id" type="INTEGER">
          <constraints nullable="false" primaryKey="true" primaryKeyName="actor_pkey"/>
        </column>
        <column name="firstname" type="VARCHAR(255)"/>
        <column name="lastname" type="VARCHAR(255)"/>
        <column name="twitter" type="VARCHAR(15)"/>
      </createTable>
    </changeSet>

  </databaseChangeLog>
  ```

- In the same directory where you created the `master-changelog.xml` file, create a file called `liquibase.properties` by executing the following command:

  ```shell
  touch liquibase.properties
  ```

  Add the following to the `liquibase.properties` file:

  ```properties
  changeLogFile:master-changelog.xml
  url: jdbc:postgresql://localhost:5433/yugabyte
  username: yugabyte
  password: yugabyte
  classpath: <relative-path-to-postgres-jar>/postgresql-42.2.8.jar:<relative-path-to-liquibase-yugabytedb-<version>-jar>/liquibase-yugabytedb-<version>.jar
  ```

  Defining the classpath is necessary if you have placed the JAR files in a folder other than `/lib`. For more information, see [Creating and configuring the liquibase.properties file](https://docs.liquibase.com/workflows/liquibase-community/creating-config-properties.html).

  When using the YugabyteDB on-premises and specifying the URL, enter your IP address or host name, and then include the port followed by the database name, as per the following format:

  ```sh
  jdbc:postgresql://<IP_OR_HOSTNAME>:<PORT>/<DATABASE>
  ```

  When specifying the classpath for the PostgreSQL driver, ensure that the version matches the version of the downloaded driver. The default username and password is `yugabyte`.

## Use Liquibase

You can run Liquibase to perform migration by executing the following command:

```shell
liquibase update
```

Optionally, you may run a different changelog than the one provided in the `liquibase.properties` file by using the following command:

```shell
liquibase --changeLogFile=different_changelog.xml update
```

Upon successful migration, the following is displayed:

![Liquibase startup](/images/ee/liquibase.png)

After migration of the first changelog, among other artifacts Liquibase creates a `databasechangelog` table in the database. This table keeps a detailed track of all the changesets that have been successfully completed.
