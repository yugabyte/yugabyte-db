---
title: Create databases
linkTitle: Create databases
description: Create databases
headcontent:
image: /images/section_icons/deploy/enterprise.png
beta: /faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    identifier: create-databases
    parent: yugabyte-cloud
    weight: 643
isTocNested: true
showAsideToc: true
---

## Create a database

### YSQL

You can use the YSQL shell (`ysqlsh`) to create and manage YugabyteDB databases. Until `ysqlsh` is available as a separate installation, you should
download YugabyteDB for your local operating system. After installing YugabyteDB locally, you can open the YSQL shell, specifying the following options:

- host
- port
- username
- password

Here's an example:

```sh
$ ./bin/ysqlsh -h
```

### YCQL




## Create a database using third party clients


## Create a database using sample datasets

Use the YSQL shell to 

[Sample datasets](../../../sample-data/) are available to help you learn and explore YugabyteDB. The SQL script files (`.sql`) required to create
sample databases are included in the `share` directory of the YugabyteDB home directory.

Here are links to documentation on the datasets and the steps to create the sample databases:

- [Northwind](../../../sample-datasest/northwind/)
- [PgExercises](../../../sample-datasest/pgexercises/)
- [SportsDB](../../../sample-datasest/sportsdb/)
- [Chinook](../../../sample-datasest/chinook/)
