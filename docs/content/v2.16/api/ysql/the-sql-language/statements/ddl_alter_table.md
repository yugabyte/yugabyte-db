---
title: ALTER TABLE statement [YSQL]
headerTitle: ALTER TABLE
linkTitle: ALTER TABLE
description: Use the `ALTER TABLE` statement to change the definition of a table.
menu:
  v2.16:
    identifier: ddl_alter_table
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER TABLE` statement to change the definition of a table.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_table,alter_table_action,alter_table_constraint,alter_column_constraint,table_expr.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_table,alter_table_action,alter_table_constraint,alter_column_constraint,table_expr.diagram.md" %}}
  </div>
</div>

<a name="table-expr-note"></a></br></br>
{{< note title="Table inheritance is not yet supported" >}}

YSQL in the present "latest" YugabyteDB does not yet support the "table inheritance" feature that is described in the [PostgreSQL documentation](https://www.postgresql.org/docs/11/ddl-inherit.html). The attempt to create a table that inherits another table causes the _0A000 (feature_not_supported)_ error with the message _"INHERITS not supported yet"_. This means that the syntax that the `table_expr` rule allows doesn't not yet bring any useful meaning.

It says that you can write, for example, this:

```plpgsql
alter table t * add column y text;
```

or this:

```plpgsql
alter table only t add column y text;
```

These variants are useful only when at least one other table inherits `t`. But as yet, no table can inherit `t`. This means that if the unadorned variant `alter table t...` runs without error, then each of these variants will run without error too. But the effect of each is the same as that of the unadorned variant. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
{{< /note >}}

## Semantics

### *alter_table_action*

Specify one of the following actions.

#### ADD [ COLUMN ] *column_name* *data_type* [*constraint*](#constraints)

Add the specified column with the specified data type and constraint.

#### RENAME TO *table_name*

Rename the table to the specified table name.

{{< note title="Note" >}}

Renaming a table is a non blocking metadata change operation.

{{< /note >}}


#### DROP [ COLUMN ] *column_name* [ RESTRICT | CASCADE ]

Drop the named column from the table.

- `RESTRICT` — Remove only the specified column.
- `CASCADE` — Remove the specified column and any dependent objects.

##### Example

Set up and populate a parents-children pair of tables:

```plpgsql
drop table if exists children cascade;
drop table if exists parents  cascade;

-- The column "b" models a (natural) business unique key.
create table parents(
  k int primary key,
  b int not null,
  v text not null,
  constraint parents_b_unq unique(b));

create table children(
  parents_b  int  not null,
  k          int  not null,
  v          text not null,

  constraint children_pk primary key(parents_b, k),

  constraint children_fk foreign key(parents_b)
    references parents(b)
    match full
    on delete cascade
    on update restrict);

insert into parents(k, b, v) values (1, 10, 'dog'), (2, 20, 'cat'), (3, 30, 'frog');

insert into children(parents_b, k, v) values
  (10, 1, 'dog-child-a'),
  (10, 2, 'dog-child-b'),
  (10, 3, 'dog-child-c'),
  (20, 1, 'cat-child-a'),
  (20, 2, 'cat-child-b'),
  (20, 3, 'cat-child-c'),
  (30, 1, 'frog-child-a'),
  (30, 2, 'frog-child-b'),
  (30, 3, 'frog-child-c');

select p.v as "p.v", c.v as "c.v"
from parents p inner join children c on c.parents_b = p.b
order by p.b, c.k;
```

This is the result:

```output
 p.v  |     c.v
------+--------------
 dog  | dog-child-a
 dog  | dog-child-b
 dog  | dog-child-c
 cat  | cat-child-a
 cat  | cat-child-b
 cat  | cat-child-c
 frog | frog-child-a
 frog | frog-child-b
 frog | frog-child-c
```

The `\d children` metacommand shows that it has a foreign key that's a dependent object on the column `b` in the `parents` table:

```output
Indexes:
    "children_pk" PRIMARY KEY, lsm (parents_b HASH, k ASC)
Foreign-key constraints:
    "children_fk" FOREIGN KEY (parents_b) REFERENCES parents(b) MATCH FULL ON UPDATE RESTRICT ON DELETE CASCADE
```

This is a contrived example. It is unusual practice (and normally bad practice) to make a foreign key constraint target anything but the column list upon which the parent table's primary key constraint is defined. But there are sometimes defensible reasons to do this.

Now try to drop the column `parents.b`:

```plpgsql
do $body$
declare
  message  text not null := '';
  detail   text not null := '';
begin
  -- Causes error 'cos "cascade" is required.
  alter table parents drop column b;
  assert false, 'Should not get here';
exception
  -- Error 2BP01
  when dependent_objects_still_exist then
    get stacked diagnostics
      message  = message_text,
      detail   = pg_exception_detail;
    assert message = 'cannot drop column b of table parents because other objects depend on it',      'Bad message';
    assert detail  = 'constraint children_fk on table children depends on column b of table parents', 'Bad detail';
end;
$body$;
```

It finishes without error, showing that the bare `alter table parents drop column b`, without `cascade`, fails and causes the message and hint that the code presents. Now repeat the attempt with `cascade` and observe the result:

```plpgsql
alter table parents drop column b cascade;
```

It quietly succeeds. Now `\d children` shows that the foreign key constraint `children_fk` has been transitively dropped.

#### ADD [*alter_table_constraint*](#constraints)

Add the specified constraint to the table.

#### DROP CONSTRAINT *constraint_name* [ RESTRICT | CASCADE ]

Drop the named constraint from the table.

- `RESTRICT` — Remove only the specified constraint.
- `CASCADE` — Remove the specified constraint and any dependent objects.

#### RENAME [ COLUMN ] *column_name* TO *column_name*

Rename a column to the specified name.

#### ENABLE / DISABLE ROW LEVEL SECURITY

This enables or disables row level security for the table.
If enabled and no policies exist for the table, then a default-deny policy is applied.
If disabled, then existing policies for the table will not be applied and will be ignored.
See [CREATE POLICY](../dcl_create_policy) for details on how to create row level security policies.

#### FORCE / NO FORCE ROW LEVEL SECURITY

This controls the application of row security policies for the table when the user is the table owner.
If enabled, row level security policies will be applied when the user is the table owner.
If disabled (the default) then row level security will not be applied when the user is the table owner.
See [CREATE POLICY](../dcl_create_policy) for details on how to create row level security policies.

### Constraints

Specify a table or column constraint.

#### CONSTRAINT *constraint_name*

Specify the name of the constraint.

#### Foreign key

`FOREIGN KEY` and `REFERENCES` specify that the set of columns can only contain values that are present in the referenced columns of the referenced table. It is used to enforce referential integrity of data.

#### Unique

This enforces that the set of columns specified in the `UNIQUE` constraint are unique in the table, that is, no two rows can have the same values for the set of columns specified in the `UNIQUE` constraint.

#### Check

This is used to enforce that data in the specified table meets the requirements specified in the `CHECK` clause.

#### Default

This is used to specify a default value for the column. If an `INSERT` statement does not specify a value for the column, then the default value is used. If no default is specified for a column, then the default is NULL.

#### Deferrable constraints

Constraints can be deferred using the `DEFERRABLE` clause. Currently, only foreign key constraints
can be deferred in YugabyteDB. A constraint that is not deferrable will be checked after every row
within a statement. In the case of deferrable constraints, the checking of the constraint can be postponed
until the end of the transaction.

Constraints marked as `INITIALLY IMMEDIATE` will be checked after every row within a statement.

Constraints marked as `INITIALLY DEFERRED` will be checked at the end of the transaction.

## See also

- [`CREATE TABLE`](../ddl_create_table)
