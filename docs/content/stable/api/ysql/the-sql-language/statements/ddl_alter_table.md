---
title: ALTER TABLE statement [YSQL]
headerTitle: ALTER TABLE
linkTitle: ALTER TABLE
description: Use the `ALTER TABLE` statement to change the definition of a table.
menu:
  stable_api:
    identifier: ddl_alter_table
    parent: statements
aliases:
  - /stable/api/ysql/commands/ddl_alter_table/
type: docs
---

## Synopsis

Use the `ALTER TABLE` statement to change the definition of a table.

## Syntax

{{%ebnf%}}
  alter_table,
  alter_table_action,
  alter_table_constraint,
  alter_column_action,
  alter_column_constraint,
  table_expr,
  sequence_options
{{%/ebnf%}}

<a name="table-expr-note"></a></br></br>
{{< note title="Table inheritance is not yet supported" >}}

YSQL in the present "latest" YugabyteDB does not yet support the "table inheritance" feature that is described in the [PostgreSQL documentation](https://www.postgresql.org/docs/15/ddl-inherit.html). The attempt to create a table that inherits another table causes the _0A000 (feature_not_supported)_ error with the message _"INHERITS not supported yet"_. This means that the syntax that the `table_expr` rule allows doesn't yet bring any useful meaning.

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

#### ADD [ COLUMN ] [ IF NOT EXISTS ] *column_name* *data_type* *constraint*

Add the specified column with the specified data type and [constraint](#constraints).

##### Table rewrites

ADD COLUMN … DEFAULT statements require a [table rewrite](#alter-table-operations-that-involve-a-table-rewrite) when the default value is a _volatile_ expression. [Volatile expressions](https://www.postgresql.org/docs/current/xfunc-volatility.html#XFUNC-VOLATILITY) can return different results for different rows, so a table rewrite is required to fill in values for existing rows. For non-volatile expressions, no table rewrite is required.

Examples of volatile expressions:

- ALTER TABLE … ADD COLUMN v1 INT DEFAULT random()
- ALTER TABLE .. ADD COLUMN v2 UUID DEFAULT gen_random_uuid()

Examples of non-volatile expressions (no table rewrite):

- ALTER TABLE … ADD COLUMN nv1 INT DEFAULT 5
- ALTER TABLE … ADD COLUMN nv2 timestamp DEFAULT now() -- uses the same timestamp now() for all existing rows

#### RENAME TO *table_name*

Rename the table to the specified table name.

{{< note title="Note" >}}

Renaming a table is a non blocking metadata change operation.

{{< /note >}}

#### SET TABLESPACE *tablespace_name*

Asynchronously change the tablespace of an existing table.

The tablespace change will immediately reflect in the config of the table, however the tablet move by the load balancer happens in the background.

While the load balancer is performing the move it is perfectly safe from a correctness perspective to do reads and writes, however some query optimization that happens based on the data location may be off while data is being moved.

##### Example

```sql
yugabyte=# ALTER TABLE bank_transactions_eu SET TABLESPACE eu_central_1_tablespace;
```

```output
NOTICE:  Data movement for table bank_transactions_eu is successfully initiated.
DETAIL:  Data movement is a long running asynchronous process and can be monitored by checking the tablet placement in http://<YB-Master-host>:7000/tables
ALTER TABLE
```

Tables can be moved to the default tablespace using:

```sql
ALTER TABLE table_name SET TABLESPACE pg_default;
```

#### SET LOGGED | UNLOGGED

Changes the table from unlogged to logged or vice-versa. Cannot be applied to a temporary table.

Currently the *UNLOGGED* option is ignored. It's handled as *LOGGED* default persistence.

#### SET ( *param_name* = *param_value* )

Change the specified storage parameter into the provided value.

Storage parameters, [as defined by PostgreSQL](https://www.postgresql.org/docs/15/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS), are ignored and only present for compatibility with PostgreSQL.

#### RESET ( *param_name* )

Reset the specified storage parameter.

Storage parameters, [as defined by PostgreSQL](https://www.postgresql.org/docs/15/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS), are ignored and only present for compatibility with PostgreSQL.

#### DROP [ COLUMN ] [ IF EXISTS ] *column_name* [ RESTRICT | CASCADE ]

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

The `\d children` meta-command shows that it has a foreign key that's a dependent object on the column `b` in the `parents` table:

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

#### ADD *alter_table_constraint*

Add the specified [constraint](#constraints) to the table.

##### Table rewrites

Adding a `PRIMARY KEY` constraint results in a full table rewrite of the main table and all associated indexes, which can be a potentially expensive operation. For more details about table rewrites, see [Alter table operations that involve a table rewrite](#alter-table-operations-that-involve-a-table-rewrite).

The table rewrite is needed because of how YugabyteDB stores rows and indexes. In YugabyteDB, data is distributed based on the primary key; when a table does not have an explicit primary key assigned, YugabyteDB automatically creates an internal row ID to use as the table's primary key. As a result, these rows need to be rewritten to use the newly added primary key column. For more information, refer to [Primary keys](/stable/develop/data-modeling/primary-keys-ysql).

#### ALTER [ COLUMN ] *column_name* [ SET DATA ] TYPE *data_type* [ COLLATE *collation* ] [ USING *expression* ]

Change the type of an existing column. The following semantics apply:

- If the optional `COLLATE` clause is not specified, the default collation for the new column type will be used.
- If the optional `USING` clause is not provided, the default conversion for the new column value will be the same as an assignment cast from the old type to the new type.
- A `USING` clause must be included when there is no implicit assignment cast available from the old type to the new type.
- Alter type is not supported for partitioned tables. See {{<issue 16980>}}.
- Alter type is not supported for tables with rules (limitation inherited from PostgreSQL).
- Alter type is not supported for tables with CDC streams if a table rewrite is required. See {{<issue 27766>}}.
- Alter type is not supported for tables under xCluster replication if a table rewrite is required. This will be supported by automatic mode in a future release. See {{<issue 27796>}}.

##### Table rewrites

Altering a column's type requires a [full table rewrite](#alter-table-operations-that-involve-a-table-rewrite), and any indexes that contain this column when the underlying storage format changes or if the data changes.

The following type changes commonly require a table rewrite:

|     From         |  To               | Reason for table rewrite                                       |
| ------------ | -------------- | --------------------------------------------------------------------- |
| INTEGER      | TEXT           | Different storage formats.                                            |
| TEXT         | INTEGER        | Needs parsing and validation.                                         |
| JSON         | JSONB          | Different internal representation.                                    |
| UUID         | TEXT           | Different binary format.                                              |
| BYTEA        | TEXT           | Different encoding.                                                   |
| TIMESTAMP    | DATE           | Loses time info; storage changes.                                     |
| BOOLEAN      | INTEGER        | Different sizes and encoding.                                         |
| REAL         | NUMERIC        | Different precision and format.                                       |
| NUMERIC(p,s) | NUMERIC(p2,s2) | Requires data changes if scale is changed or if precision is smaller. |

The following type changes do not require a rewrite when there is no associated index table on the column. When there is an associated index table on the column, a rewrite is performed on the index table alone but not on the main table.

| From              |  To                   | Notes                  |
| ------------ | ------------------ | ------------------------------------------------------ |
| VARCHAR(n)   | VARCHAR(m) (m > n) | Length increase is compatible.                         |
| VARCHAR(n)   | TEXT               | Always compatible.                                     |
| SERIAL       | INTEGER            | Underlying type is INTEGER; usually OK.                |
| NUMERIC(p,s) | NUMERIC(p2,s2)     | If new precision is larger and scale remains the same. |
| CHAR(n)      | CHAR(m) (m > n)    | PG stores it as padded TEXT, so often fine.            |
| Domain types | Their base type    | Compatible, unless additional constraints exist.       |

Altering a column with a (non-trivial) USING clause always requires a rewrite.

The table rewrite operation preserves split properties for hash-partitioned tables and hash-partitioned secondary indexes. For range-partitioned tables (and secondary indexes), split properties are only preserved if the altered column is not part of the table's (or secondary index's) range key.

For example, the following ALTER TYPE statements would cause a table rewrite:

- ALTER TABLE foo
    ALTER COLUMN foo_timestamp TYPE timestamp with time zone
    USING
        timestamp with time zone 'epoch' + foo_timestamp * interval '1 second';
- ALTER TABLE t ALTER COLUMN t_num1 TYPE NUMERIC(9,5) -- from NUMERIC(6,1);
- ALTER TABLE test ALTER COLUMN a SET DATA TYPE BIGINT USING a::BIGINT; -- from INT

The following ALTER TYPE statement does not cause a table rewrite:

- ALTER TABLE test ALTER COLUMN a TYPE VARCHAR(51); -- from VARCHAR(50)

#### DROP CONSTRAINT *constraint_name* [ RESTRICT | CASCADE ]

Drop the named constraint from the table.

- `RESTRICT` — Remove only the specified constraint.
- `CASCADE` — Remove the specified constraint and any dependent objects.

##### Table rewrites

Dropping the `PRIMARY KEY` constraint results in a full table rewrite and full rewrite of all indexes associated with the table, which is a potentially expensive operation. For more details and common limitations of table rewrites, refer to [Alter table operations that involve a table rewrite](#alter-table-operations-that-involve-a-table-rewrite).

#### RENAME [ COLUMN ] *column_name* TO *column_name*

Rename a column to the specified name.

#### RENAME CONSTRAINT *constraint_name* TO *constraint_name*

Rename a constraint to the specified name.

##### Example

Create a table with a constraint and rename the constraint:

```sql
CREATE TABLE test(id BIGSERIAL PRIMARY KEY, a TEXT);
ALTER TABLE test ADD constraint vague_name unique (a);
ALTER TABLE test RENAME CONSTRAINT vague_name TO unique_a_constraint;
```

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

## Alter table operations that involve a table rewrite

Most ALTER TABLE statements only involve a schema modification and complete quickly. However, certain specific ALTER TABLE statements require a new copy of the underlying table (and associated index tables, in some cases) to be made and can potentially take a long time, depending on the sizes of the tables and indexes involved. This is typically referred to as a "table rewrite". This behavior is [similar to PostgreSQL](https://www.crunchydata.com/blog/when-does-alter-table-require-a-rewrite), though the exact scenarios when a rewrite is triggered may differ between PostgreSQL and YugabyteDB.

It is not safe to execute concurrent DML on the table during a table rewrite because the results of any concurrent DML are not guaranteed to be reflected in the copy of the table being made. This restriction is similar to PostgreSQL, which explicitly prevents concurrent DML during a table rewrite by acquiring an ACCESS EXCLUSIVE table lock.

If you need to perform one of these expensive rewrites, it is recommended to combine them into a single ALTER TABLE statement to avoid multiple expensive rewrites. For example:

```sql
ALTER TABLE t ADD COLUMN c6 UUID DEFAULT gen_random_uuid(), ALTER COLUMN c8 TYPE TEXT
```

The following ALTER TABLE operations involve making a full copy of the underlying table (and possibly associated index tables):

1. [Adding](#add-alter) or [dropping](#drop-constraint-constraint-restrict-cascade) the primary key of a table.
1. [Adding a column with a (volatile) default value](#add-column-if-not-exists-column-data-constraint).
1. [Changing the type of a column](#alter-column-column-set-data-type-data-collate-collation-using-expression).

## See also

- [CREATE TABLE](../ddl_create_table)
