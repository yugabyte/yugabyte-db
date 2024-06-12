---
title: Session-level audit logging in YSQL
headerTitle: Session-level audit logging in YSQL
linkTitle: Session-level audit logging
description: Session-level audit logging in YSQL.
menu:
  v2.20:
    identifier: session-audit-logging-1-ysql
    parent: audit-logging
    weight: 760
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../session-audit-logging-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Session logging is enabled on a per user session basis.

To enable session logging for all DML and DDL statements and log all relations in DML statements, you would enter the following commands:

```sql
set pgaudit.log = 'write, ddl';
set pgaudit.log_relation = on;
```

Enable session logging for all commands except MISC and raise audit log messages as NOTICE.

## Session-level example

In this example, session audit logging is used for logging DDL and SELECT statements. Note that the insert statement is not logged because the WRITE class is not enabled.

SQL statements are shown below.

### Setup

{{% explore-setup-single %}}

Connect to the database using ysqlsh and enable the `pgaudit` extension on the YugabyteDB cluster as follows:

```sql
\c yugabyte yugabyte;
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### Enable session audit logging

Enable session audit logging in the YugabyteDB cluster as follows:

```sql
SET pgaudit.log = 'read, ddl';
```

### Perform statements

Run some statements as follows:

```sql
CREATE TABLE account
(
    id int,
    name text,
    password text,
    description text
);

INSERT INTO account (id, name, password, description)
             VALUES (1, 'user1', 'HASH1', 'blah, blah');

SELECT * FROM account;
```

### Verify output

You should see output similar to the following in the logs:

```output
2020-11-09 19:19:09.262 UTC [3710] LOG:  AUDIT: SESSION,1,1,DDL,CREATE
TABLE,TABLE,public.account,"create table account
        (
            id int,
            name text,
            password text,
            description text
        );",<not logged>
2020-11-09 19:19:19.619 UTC [3710] LOG:  AUDIT: SESSION,2,1,READ,SELECT,,,"select *
            from account;",<not logged>
```
