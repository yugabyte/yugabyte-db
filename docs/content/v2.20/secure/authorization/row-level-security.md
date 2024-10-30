---
title: Row-level security (RLS)
description: Row-level security (RLS) in YugabyteDB
menu:
  v2.20:
    name: Row-level security
    identifier: ysql-row-level-security
    parent: authorization
    weight: 745
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../row-level-security/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

In addition to database access permissions available through the ROLE and GRANT privilege system, YugabyteDB provides a more granular level of security where tables can have **row security policies** that restrict rows users can access.

Row-level security (RLS) restricts rows that can be returned by normal queries or inserted, updated, or deleted by DML commands. RLS policies can be created specific to a DML command or with ALL commands. They can also be used to create policies on a particular role or multiple roles.

By default, tables do not have any row-level policies defined; if a user has access privileges to a table, all rows in the table are available to query and update.

This example uses the row-level security policies to restrict employees to view only rows that have their respective names.

## Step 1. Create example table

Open the YSQL shell (`ysqlsh`), specifying the `yugabyte` user and prompting for the password.

```sh
$ ./ysqlsh -U yugabyte -W
```

When prompted for the password, enter the yugabyte password. You should be able to log in and see a response similar to the following:

```output
ysqlsh (11.2-YB-2.5.0.0-b0)
Type "help" for help.

yugabyte=#
```

Create a employee table and insert few sample rows

```sql
create table employees ( empno int, ename text, address text, salary int,
                         account_number text );

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

## Step 2. Grant access to users

Set up the database by creating users based on the entries in rows and provide table access to them.

```sql
create user joe;
grant select on employees to joe;

create user mike;
grant select on employees to mike;

create user julia;
grant select on employees to julia;
```

At this point, users can see all the data.

```sql
\c yugabyte joe;
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     1 | Joe   | 56 grove st   |  20000 | AC-22001
     3 | Julia | 1 finite loop |  40000 | AC-77051
     2 | Mike  | 129 81 st     |  80000 | AC-48901
(3 rows)
```

```sql
\c yugabyte mike;
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     1 | Joe   | 56 grove st   |  20000 | AC-22001
     3 | Julia | 1 finite loop |  40000 | AC-77051
     2 | Mike  | 129 81 st     |  80000 | AC-48901
(3 rows)
```

```sql
\c yugabyte julia;
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     1 | Joe   | 56 grove st   |  20000 | AC-22001
     3 | Julia | 1 finite loop |  40000 | AC-77051
     2 | Mike  | 129 81 st     |  80000 | AC-48901
(3 rows)
```

## Step 3. Set up RLS for a user

Now create a row-level security policy for user `joe`

```sql
\c yugabyte yugabyte;

CREATE POLICY emp_rls_policy ON employees FOR ALL TO PUBLIC USING (
           ename=current_user);
```

Syntax of the `CREATE POLICY` command is as follows:

* Use the `CREATE POLICY` command to create the policy. Need to be a superuser to execute this command.
* `emp_rls_policy` is the user defined name for the policy.
* `employees` is the name of the table.
* `ALL` here represents all DDL commands. Alternatively one can specify select, insert, update, delete, or other operations that need to be restricted.
* `PUBLIC` here represents all roles. Alternatively one can provide specific role names to which the policy will apply.
* `USING (ename=current_user)`is called the expression. It is a filter condition that returns a boolean value. This command compares the `ename` column of the `employees` tables to the **logged in** user, if they match then the user will be able to access the row for DDL operations.

## Step 4. Enable RLS on table

Enable row-level security on the table

```sql
\c yugabyte yugabyte;

ALTER TABLE employees ENABLE ROW LEVEL SECURITY;
```

## Step 5. Verify row-level security

Verify what each user can view from the employees table.

```sql
\c yugabyte yugabyte;
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     2 | mike  | 129 81 st     |  80000 | AC-48901
     1 | joe   | 56 grove st   |  20000 | AC-22001
     3 | julia | 1 finite loop |  40000 | AC-77051
(3 rows)
```

```sql
\c yugabyte joe;

select current_user;
```

```output
 current_user
--------------
 joe
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename |   address   | salary | account_number
-------+-------+-------------+--------+----------------
     1 | joe   | 56 grove st |  20000 | AC-22001
(1 row)
```

```sql
\c yugabyte mike;

select current_user;
```

```output
 current_user
--------------
 mike
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename |  address  | salary | account_number
-------+-------+-----------+--------+----------------
     2 | mike  | 129 81 st |  80000 | AC-48901
(1 row)
```

```sql
\c yugabyte julia

select current_user;
```

```output
 current_user
--------------
 julia
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     3 | julia | 1 finite loop |  40000 | AC-77051
(1 row)
```

As defined in the policy, the `current_user` can only access their own row.

## Step 6. Bypass row-level security

YugabyteDB has **BYPASSRLS** and **NOBYPASSRLS** permissions, which can be assigned to a role. By default, table owner and superuser have `BYPASSRLS` permissions assigned, so these users can skip the row-level security. The other roles in a database will have `NOBYPASSRLS` assigned to them by default.

Assign `BYPASSRLS` to user `joe` so they can see all the rows in the employees table.

```sql
\c yugabyte yugabyte;

ALTER USER joe BYPASSRLS;

\c yugabyte joe;

select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     2 | mike  | 129 81 st     |  80000 | AC-48901
     1 | joe   | 56 grove st   |  20000 | AC-22001
     3 | julia | 1 finite loop |  40000 | AC-77051
(3 rows)
```

## Step 7. Remove row-level policy

`DROP POLICY` command is used to drop a policy

```sql
\c yugabyte yugabyte;

DROP POLICY emp_rls_policy ON employees;
```

Logging in as user `joe` or `julia` won't return any data because the RLS policy was dropped and row-level security is still enabled on the table.

```sql
\c yugabyte mike;

select current_user;
```

```output
 current_user
--------------
 mike
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename | address | salary | account_number
-------+-------+---------+--------+----------------
(0 rows)
```

To completely disable row-level security on the table, `ALTER TABLE` to remove row-level security.

```sql
\c yugabyte yugabyte;

ALTER TABLE employees DISABLE ROW LEVEL SECURITY;

\c yugabyte mike;

select current_user;
```

```output
 current_user
--------------
 mike
(1 row)
```

```sql
select * from employees;
```

```output
 empno | ename |    address    | salary | account_number
-------+-------+---------------+--------+----------------
     2 | mike  | 129 81 st     |  80000 | AC-48901
     1 | joe   | 56 grove st   |  20000 | AC-22001
     3 | julia | 1 finite loop |  40000 | AC-77051
(3 rows)
```
