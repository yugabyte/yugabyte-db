---
title: YSQL Loader
linkTitle: YSQL Loader
description: YSQL Loader
aliases:
menu:
  latest:
    identifier: ysql-loader
    parent: integrations
    weight: 577
isTocNested: true
showAsideToc: true
---

YSQL Loader is a data migration tool based on [pgloader](https://pgloader.readthedocs.io/en/latest/intro.html). You can use YSQL Loader to load data from different sources into YugabyteDB. In addition to the functionality provided by pgloader, YSQL Loader supports dumping DDLs and reading modified DDLs which enables you to use YugabyteDB-specific constructs such as tablets, for example.


## Loading data from MySQL

To load data from MySQL, you need to supply a configuration file similar to the following:

```shell
load database 
  from mysql://root:password@localhost:3306/northwind 
  into postgresql://yugabyte:yugabyte@localhost:5433/northwindmysql
WITH
  max parallel create index=1
;
```

You do not have to specify any other options, as they function exactly as in [pgloader](https://pgloader.readthedocs.io/en/latest/intro.html).

## Modifying DDL

You can modify the DDL by performing the following steps:

1. Dump the DDL using a configuration file similar to the following:

   ```shell
   load database 
     from mysql://root:password@localhost:3306/northwind 
     into postgresql://yugabyte:yugabyte@localhost:5433/northwindmysql
   WITH
     dumpddl only,
     max parallel create index=1
   ;
   ```

2. Modify the the `ddl.sql` DDL file and run it using the [ysqlsh](https://docs.yugabyte.com/latest/admin/ysqlsh/) command-line tool.  
3. Provide the DDL file using a configuration file similar to the following:

   ```shell
   load database 
    from mysql://root:password@localhost:3306/northwind 
    into postgresql://yugabyte:yugabyte@localhost:5433/northwindmysql
   WITH
    data only,
    max parallel create index=1
   ;
   ```

Alternatively, you can execute a configuration file similar to the following, in which case you do need to run the `ddl.sql` file using the ysqlsh tool:

```shell
load database 
  from mysql://root:password@localhost:3306/northwind 
  into postgresql://yugabyte:yugabyte@localhost:5433/northwindmysql
WITH
  data only,
  max parallel create index=1
  
BEFORE LOAD EXECUTE
  '/Users/myname/quicklisp/local-projects/pgloader/ddl.sql  
;
```

