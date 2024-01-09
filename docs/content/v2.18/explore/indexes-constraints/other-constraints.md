---
title: Other constraints in YugabyteDB YSQL
headerTitle: Other constraints
linkTitle: Other constraints
description: Other constraints in YSQL
headContent: Explore other constraints in YugabyteDB using YSQL
menu:
  v2.18:
    identifier: other-constraints
    parent: explore-indexes-constraints
    weight: 270
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../other-constraints/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

{{% explore-setup-single %}}

## CHECK Constraint

The YSQL `CHECK` constraint allows you to constrain values in columns based on a boolean expression. The values are evaluated with regards to meeting a specific requirement before these values are inserted or updated; if they fail the check, YSQL rejects the changes and displays a constraint violation error.

In most cases, you add the `CHECK` constraint when you create a table, as demonstrated by the following example:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  birth DATE CHECK (birth > '1940-01-01'),
  salary numeric CHECK (salary > 10)
);
```

The following example attempts to insert a row that violates the `CHECK` constraint into the `employees` table:

```sql
INSERT INTO employees (employee_no, name, department, birth, salary)
  VALUES (2001, 'Hugh Grant', 'Sales', '1963-05-05', 0);
```

The following output shows that the execution of the `INSERT` statement failed because of the `CHECK` constraint on the `salary` column, which only accepts values greater than 10:

```output
ERROR: new row for relation "employees" violates check constraint "employees_salary_check"
DETAIL: Failing row contains (2001, Hugh Grant, Sales, 1963-05-05, 0).
```

The preceding output shows the name of the `CHECK` constraint as `employees_salary_check` which was assigned by default based on the table_column_check pattern. If you need a specific name for the `CHECK` constraint, you can set it, as per the following example:

```sql
(
  ...
  salary numeric CONSTRAINT fair_salary CHECK (salary > 10)
  ...
);
```

YSQL also allows you to add `CHECK` constraints to existing tables by using the `ALTER TABLE` statement. The following example shows how to add a length check for the employee name in the `employees` table:

```sql
ALTER TABLE employees
  ADD CONSTRAINT name_check CHECK (char_length(name) <= 3);
```

For additional examples, see:

- [Table with CHECK constraint](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-check-constraint)
- [Create indexes and check constraints on JSON columns](../../../api/ysql/datatypes/type_json/create-indexes-check-constraints/#check-constraints-on-jsonb-columns)

## UNIQUE Constraint

The `UNIQUE` constraint allows you to ensure that values stored in columns are unique across rows in a table. When inserting new rows or updating existing ones, the `UNIQUE` constraint checks if the value is already in the table, in which case the change is rejected and an error is displayed.

When you add a `UNIQUE` constraint to one or more columns, YSQL automatically creates a [unique index](../unique-index-ysql) on these columns.

The following example creates a table with a `UNIQUE` constraint for the `phone` column:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer UNIQUE
);
```

The following example creates the same constraint for the same column of the same table, only as a table constraint:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer,
  UNIQUE(phone)
);
```

The following example creates a `UNIQUE` constraint on a group of columns in a new table:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  phone integer,
  email text
  UNIQUE(phone, email)
);
```

For additional examples, see [Table with UNIQUE constraint](../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-unique-constraint).

## NOT NULL Constraint

YSQL provides a `NOT NULL` constraint as a means to control whether or not a column can accept `NULL` values. If a column has a `NOT NULL` constraint set, any attempt to insert a `NULL` value or update it with a `NULL` value results in an error.

For additional information and examples, see the following:

- [Define NOT NULL constraint](../../ysql-language-features/data-manipulation/#define-not-null-constraint)
- [Not-Null Constraints in PostgreSQL documentation](https://www.postgresql.org/docs/11/ddl-constraints.html#id-1.5.4.5.6)
