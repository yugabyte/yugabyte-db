---
title: Column-level security
description: Column-level security in YugabyteDB
menu:
  v2.14:
    name: Column-level security
    identifier: ysql-column-level-security
    parent: authorization
    weight: 755
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../column-level-security/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

Column level security in YugabyteDB is used to restrict the users to view a particular column or set of columns in a table. The simplest way to achieve column-level security in YugabyteDB is to **create a view** that includes only the columns that the user needs to have access to.

The steps below show how to enable column-level security using `CREATE VIEW` command.

## Step 1. Create example table

Open the YSQL shell (ysqlsh), specifying the `yugabyte` user and prompting for the password.

```sh
./ysqlsh -U yugabyte -W
```

When prompted for the password, enter the yugabyte password. You should be able to log in and see a response like below.

```output
ysqlsh (11.2-YB-2.5.0.0-b0)
Type "help" for help.
yugabyte=#
```

Create an employee table and insert a few sample rows.

```sql
create table employees ( empno int, ename text,
                         address text, salary int, account_number text );

insert into employees values (1, 'joe', '56 grove st',  20000, 'AC-22001' );
insert into employees values (2, 'mike', '129 81 st',  80000, 'AC-48901' );
insert into employees values (3, 'julia', '1 finite loop',  40000, 'AC-77051');

select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     1 | Joe   | 56 grove st   |  20000 | AC-22001
     2 | Mike  | 129 81 st     |  80000 | AC-48901
     3 | Julia | 1 finite loop |  40000 | AC-77051
(3 rows)
```

## Step 2. Create `ybadmin` user

Create a user `ybadmin` and provide all privileges on the table to `ybadmin` user.

```sql
\c yugabyte yugabyte;

create user ybadmin;

GRANT ALL PRIVILEGES ON employees TO ybadmin;
```

Connect to the database with the `ybadmin` user.

```sql
\c yugabyte ybadmin;
```

## Step 3. Verify permissions

User `ybadmin `has access to view all the contents of the table.

```sql
select current_user;
```

```output
 current_user
--------------
 ybadmin
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     1 | joe   | 56 grove st   |  20000 | AC-22001
     3 | julia | 1 finite loop |  40000 | AC-77051
     2 | mike  | 129 81 st     |  80000 | AC-48901
(3 rows)
```

## Step 4a. Restrict column access using `CREATE VIEW`

Admin user `ybadmin` has permissions to view all the contents on employees table. In order to prevent admin users from viewing sensitivity information like salary and account_number, the `CREATE VIEW` statement can be used to secure the sensitive columns.

```sql
\c yugabyte yugabyte;

REVOKE SELECT ON employees FROM ybadmin;
CREATE VIEW emp_info as select empno, ename, address from employees;
GRANT SELECT on emp_info TO ybadmin;
```

### Verify access privileges

Verify the permissions of the ybadmin user on the employee table.

```sql
\c yugabyte ybadmin;

select current_user;
```

```output
 current_user
--------------
 ybadmin
(1 row)
```

Since permission is revoked for `ybadmin`, this user will not be able to query employees table.

```sql
select * from employees;
```

```output
ERROR:  permission denied for table employees
```

Since `ybadmin` was granted select permission on `emp_info` table, `ybadmin` user will be able to query `emp_info` table.

```sql
select * from emp_info;
```

```output
 empno | ename |    address
-------+-------+---------------
     1 | joe   | 56 grove st
     3 | julia | 1 finite loop
     2 | mike  | 129 81 st
(3 rows)
```

## Step 4b. Restrict column access using `GRANT`

Instead of creating views, YugabyteDB supports column level permissions, where users can be provided access to select columns in a table using `GRANT` command.

Considering the above example, instead of creating a new view, `ybadmin` user can be provided with permissions to view all columns except salary and account_number like below.

```sql
\c yugabyte yugabyte;

grant select (empno, ename, address) on employees to ybadmin;
```

### Verify access privileges

User `ybadmin` will now be able to access the columns to which permissions were granted.

```sql
\c yugabyte ybadmin;

select empno, ename, address from employees;
```

```output
 empno | ename |    address
-------+-------+---------------
     1 | joe   | 56 grove st
     3 | julia | 1 finite loop
     2 | mike  | 129 81 st
(3 rows)
```

`ybadmin` will still be denied if user tries to access other columns.

```sql
select empno, ename, address, salary from employees;
```

```output
ERROR:  permission denied for table employees
```
