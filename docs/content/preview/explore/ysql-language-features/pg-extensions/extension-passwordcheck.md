---
title: passwordcheck extension
headerTitle: passwordcheck extension
linkTitle: passwordcheck
description: Using the passwordcheck extension in YugabyteDB
menu:
  preview:
    identifier: extension-passwordcheck
    parent: pg-extensions
    weight: 20
type: docs
---

The [passwordcheck](https://www.postgresql.org/docs/11/passwordcheck.html) PostgreSQL module provides a means to check user passwords whenever they are set with CREATE ROLE or ALTER ROLE. If a password is considered too weak, it will be rejected and the command will terminate with an error.

## Enable passwordcheck

To enable the passwordcheck extension, add `passwordcheck` to `shared_preload_libraries` in the PostgreSQL server configuration parameters using the YB-TServer [--ysql_pg_conf_csv](../../../../reference/configuration/yb-tserver/#ysql-pg-conf-csv) flag:

```sh
--ysql_pg_conf_csv="shared_preload_libraries=passwordcheck"
```

Note that modifying `shared_preload_libraries` requires restarting the YB-TServer.

## Customize passwordcheck

You can customize the following passwordcheck parameters:

| Parameter | Description | Default |
| :--- | :--- | :--- |
| minimum_length | Minimum password length. | 8 |
| maximum_length | Maximum password length. | 15 |
| restrict_lower | Passwords must include a lowercase character. | true |
| restrict_upper | Passwords must include an uppercase character. | true |
| restrict_numbers | Passwords must include a number. | true |
| restrict_special | Passwords must include a special character. | true |
| special_chars | The set of special characters. | <code>!@#$%^&*()_+{}\|\<\>?=</code> |

For example, the following flag changes the minimum and maximum passwordcheck lengths:

```sh
--ysql_pg_conf_csv="shared_preload_libraries=passwordcheck,passwordcheck.minimum_length=10,passwordcheck.maximum_length=18"
```

## Example

You can change passwordcheck parameters for the _current session only_ using a `SET` statement. For example, to increase the maximum length allowed and not require numbers, execute the following commands:

```sql
SET passwordcheck.maximum_length TO 20;
SET passwordcheck.restrict_numbers TO false;
```

When enabled, if a password is considered too weak, it's rejected with an error. For example:

```sql
yugabyte=# create role test_role password 'tooshrt';
```

```output
ERROR:  password is too short
```

```sql
yugabyte=# create role test_role password 'nonumbers';
```

```output
ERROR:  password must contain both letters and nonletters
```

```sql
yugabyte=# create role test_role password '12test_role12';
```

```output
ERROR:  password must not contain user name
```

The passwordcheck extension only works for passwords that are provided in plain text. For more information, refer to the [PostgreSQL passwordcheck documentation](https://www.postgresql.org/docs/11/passwordcheck.html).
