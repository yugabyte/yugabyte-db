---
title: Object-level audit logging in YSQL
headerTitle: Object-level audit logging in YSQL
linkTitle: Object-level audit logging
description: Object-level audit logging in YSQL.
menu:
  preview:
    identifier: object-audit-logging-1-ysql
    parent: audit-logging
    weight: 765
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../object-audit-logging-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Object audit logging logs statements that affect a particular relation. Only SELECT, INSERT, UPDATE, and DELETE commands are supported. TRUNCATE is not included in object audit logging.

Object audit logging is intended to be a finer-grained replacement for `pgaudit.log = 'read, write'`. As such, it may not make sense to use them in conjunction but one possible scenario would be to use session logging to capture each statement and then supplement that with object logging to get more detail about specific relations.

In YugabyteDB, object-level audit logging is implemented by reusing the PG role system. The `pgaudit.role` setting defines the role that will be used for audit logging. A relation ( TABLE, VIEW, etc.) will be audit logged when the audit role has permissions for the command executed or inherits the permissions from another role. This allows you to effectively have multiple audit roles even though there is a single master role in any context.

## Object-level example

In this example object audit logging is used to illustrate how a granular approach may be taken towards logging of SELECT and DML statements.

### Setup

{{% explore-setup-single %}}

Using ysqlsh, connect to the database and enable the `pgaudit` extension on the YugabyteDB cluster as follows:

```sql
\c yugabyte yugabyte;
CREATE EXTENSION IF NOT EXISTS pgaudit;
```

### Enable object auditing

Set [pgaudit.role](https://github.com/pgaudit/pgaudit/blob/master/README.md#pgauditrole) to `auditor` and grant `SELECT` and `UPDATE` privileges on the `account` table. Any `SELECT` or `UPDATE` statements on the `account` table will now be logged. Note that logging on the `account` table is controlled by column-level permissions, while logging on the `account_role_map` table is table-level.

```sql
CREATE ROLE auditor;

SET pgaudit.role = 'auditor';
```

### Create a table

```sql
CREATE TABLE account
(
    id int,
    name text,
    password text,
    description text
);

GRANT SELECT (password)
   ON public.account
   TO auditor;

SELECT id, name FROM account;

SELECT password FROM account;

GRANT UPDATE (name, password)
   ON public.account
   TO auditor;

UPDATE account
   SET description = 'yada, yada';

UPDATE account
   SET password = 'HASH2';

CREATE TABLE account_role_map
(
    account_id int,
    role_id int
);

GRANT SELECT
   ON public.account_role_map
   TO auditor;

SELECT account.password,
       account_role_map.role_id
  FROM account
       INNER JOIN account_role_map
            ON account.id = account_role_map.account_id;
```

### Verify output

You should see the following output in the logs:

```output
2020-11-09 19:46:42.633 UTC [3944] LOG:  AUDIT: OBJECT,1,1,READ,SELECT,TABLE,public.account,"select password
          from account;",<not logged>
2020-11-09 19:47:02.531 UTC [3944] LOG:  AUDIT: OBJECT,2,1,WRITE,UPDATE,TABLE,public.account,"update account
           set password = 'HASH2';",<not logged>
I1109 19:47:09.418772  3944 ybccmds.c:453] Creating Table yugabyte.public.account_role_map
I1109 19:47:09.418812  3944 pg_ddl.cc:310] PgCreateTable: creating a transactional table: yugabyte.account_role_map
I1109 19:47:09.538868  3944 table_creator.cc:307] Created table yugabyte.account_role_map of type PGSQL_TABLE_TYPE
2020-11-09 19:47:22.752 UTC [3944] LOG:  AUDIT: OBJECT,3,1,READ,SELECT,TABLE,public.account,"select account.password,
               account_role_map.role_id
          from account
               inner join account_role_map
                    on account.id = account_role_map.account_id;",<not logged>
2020-11-09 19:47:22.752 UTC [3944] LOG:  AUDIT: OBJECT,3,1,READ,SELECT,TABLE,public.account_role_map,"select account.password,
               account_role_map.role_id
          from account
               inner join account_role_map
                    on account.id = account_role_map.account_id;",<not logged>
```
