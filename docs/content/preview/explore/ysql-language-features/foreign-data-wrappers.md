---
title: Foreign Data Wrappers
linkTitle: Foreign Data Wrappers
description: Foreign Data Wrappers in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-foreign-data-wrappers
    parent: explore-ysql-language-features
    weight: 100
isTocNested: true
showAsideToc: true
---

This document describes how to use foreign data wrappers.

## Overview

A foreign data wrapper is a library that can be used to access and interact with an external data (foreign data) source. They allow us to query foreign objects from remote servers as if they were local objects. 

In order to access foreign data, we first need to create the foreign data wrapper. Then, we need to create a foreign server, which specifies how to connect to the external data source. We may also need to create a user mapping which defines the mapping of a specific user to authorization credentials in the foreign server. Lastly, we need to create foreign tables, which represent the structure of the data on the external source. 

### CREATING FOREIGN DATA WRAPPERS

Foreign data wrappers can be created using the `CREATE FOREIGN DATA WRAPPER` command.
Only superusers or users with the yb_fdw role can create foreign data wrappers.

Example:

```
yugabyte=# CREATE FOREIGN DATA WRAPPER mywrapper HANDLER myhandler OPTIONS (dummy 'true');
```

### CREATING A FOREIGN SERVER

Foreign servers can be used to specify connection information for an external data source.
A foreign server can be created using the `CREATE FOREIGN SERVER` command.

Example:

```
yugabyte=# CREATE SERVER myserver FOREIGN DATA WRAPPER mywrapper OPTIONS (dummy 'true');
```

### CREATE USER MAPPINGS

User mappings can be used to define the mapping of a user to authorization credentials in the foreign server.
A user mapping can be created using the `CREATE USER MAPPING` command.

List all databases using the following statements.

```sql
yugabyte=# CREATE USER MAPPING FOR myuser SERVER myserver OPTIONS (user 'john', password 'password');
```

### CREATE FOREIGN TABLES

Use the `CREATE FOREIGN TABLE` command to create foreign tables.

```sql
yugabyte=# CREATE FOREIGN TABLE mytable (col1 int, col2 int) SERVER myserver OPTIONS (dummy 'true');
```


The following foreign data wrappers are bundled with YB:
- postgres_fdw
- file_fdw