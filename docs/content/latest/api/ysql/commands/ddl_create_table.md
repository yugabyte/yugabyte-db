---
title: CREATE TABLE
linkTitle: CREATE TABLE
summary: Create a new table in a database
description: CREATE TABLE
menu:
  latest:
    identifier: api-ysql-commands-create-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_table/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `CREATE TABLE` statement to create a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_table,table_elem,column_constraint,table_constraint,storage_parameters,storage_parameter,index_parameters,references_clause.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_table,table_elem,column_constraint,table_constraint,storage_parameters,storage_parameter,index_parameters,references_clause.diagram.md" /%}}
  </div>
</div>

## Semantics

### *create_table*

#### CREATE TABLE [ IF NOT EXISTS ] *table_name*

Create a table with *table_name*. An error is raised if `qualified_name` already exists in the specified database.

### *table_elem*

### *column_constraint*

#### CONSTRAINT *constraint_name*

Specify the name of the constraint.

### *table_constraint*

#### CONSTRAINT *constraint_name*

##### NOT NULL | NULL | CHECK ( *expression* ) | DEFAULT *expression* | UNIQUE index_parameters | PRIMARY KEY | *references_clause*

###### PRIMARY KEY

- Currently defining a primary key is required.
- Primary key can be defined in either `column_constraint` or `table_constraint`, but not in both.
- Each row in a table is uniquely identified by its primary key.

###### FOREIGN KEY

Foreign keys are supported starting v1.2.10.

### *storage_parameter*

Represent storage parameters [as defined by PostgreSQL](https://www.postgresql.org/docs/11/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS).

#### *name* | *name* = *value*

For DEFAULT keyword must be of the same type as the column it modifies. It must be of type boolean for CHECK constraints.

## Examples

### Table with primary key

```sql
postgres=# CREATE TABLE sample(k1 int,
                               k2 int,
                               v1 int,
                               v2 text,
                               PRIMARY KEY (k1, k2));
```

### Table with check constraint

```sql
postgres=# CREATE TABLE student_grade(student_id int,
                                      class_id int,
                                      term_id int,
                                      grade int CHECK (grade >= 0 AND grade <= 10),
                                      PRIMARY KEY (student_id, class_id, term_id));
```

### Table with default value

```sql
postgres=# CREATE TABLE cars(id int PRIMARY KEY,
                             brand text CHECK (brand in ('X', 'Y', 'Z')),
                             model text NOT NULL,
                             color text NOT NULL DEFAULT 'WHITE' CHECK (color in ('RED', 'WHITE', 'BLUE')));
```

### Table with foreign key constraint

Define two tables with a foreign keys constraint.
```sql
postgres=# CREATE TABLE products(id int PRIMARY KEY,
                                 descr text);
postgres=# CREATE TABLE orders(id int PRIMARY KEY,
                               pid int REFERENCES products(id) ON DELETE CASCADE,
                               amount int);

```

Insert some rows.
```sql
postgres=# SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE;
postgres=# INSERT INTO products VALUES (1, 'Phone X'), (2, 'Tablet Z');
postgres=# INSERT INTO orders VALUES (1, 1, 3), (2, 1, 3), (3, 2, 2);

postgres=# SELECT o.id AS order_id, p.id as product_id, p.descr, o.amount FROM products p, orders o WHERE o.pid = p.id;
```
```
order_id | product_id |  descr   | amount
----------+------------+----------+--------
        1 |          1 | Phone X  |      3
        2 |          1 | Phone X  |      3
        3 |          2 | Tablet Z |      2
(3 rows)
```

Inserting a row referencing a non-existent product is not allowed.
```sql
postgres=# INSERT INTO orders VALUES (1, 3, 3);
```
```
ERROR:  insert or update on table "orders" violates foreign key constraint "orders_pid_fkey"
DETAIL:  Key (pid)=(3) is not present in table "products".
```

Deleting a product will cascade to all orders (as defined in the `CREATE TABLE` statement above).
```sql
postgres=# DELETE from products where id = 1;
postgres=# SELECT o.id AS order_id, p.id as product_id, p.descr, o.amount FROM products p, orders o WHERE o.pid = p.id;
```
```
 order_id | product_id |  descr   | amount
----------+------------+----------+--------
        3 |          2 | Tablet Z |      2
(1 row)
```

### Table with unique constraint

```sql
postgres=# CREATE TABLE translations(message_id int UNIQUE,
                                     message_txt text);
```


## See also

- [`ALTER TABLE`](../ddl_alter_table)
- [`CREATE TABLE AS`](../ddl_create_table_as)
- [`CREATE TABLESPACE`](../ddl_create_tablespace)
- [`DROP TABLE`](../ddl_drop_table)
