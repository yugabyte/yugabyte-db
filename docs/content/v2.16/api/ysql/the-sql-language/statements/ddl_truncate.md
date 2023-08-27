---
title: TRUNCATE
headerTitle: TRUNCATE
linkTitle: TRUNCATE
description:  Use the TRUNCATE statement to remove all rows from one or several tables.
menu:
  v2.16:
    identifier: ddl_truncate
    parent: statements
type: docs
---

## Synopsis

Use the `TRUNCATE` statement to remove all rows from the specified table or, optionally, to remove all rows from all the tables in the closure with foreign key references to the specified table.

Applying `TRUNCATE` to a set of tables produces the same ultimate outcome as does using an unrestricted `DELETE` on each table in the set; but it doesn't scan the tables. `TRUNCATE` is therefore faster than `DELETE`. It also reclaims disk space immediately. This means that the larger is the table, the more greater the performance benefit of `TRUNCATE` is.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/truncate.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/truncate.diagram.md" %}}
  </div>
</div>

{{< note title="Table inheritance is not yet supported" >}}
The [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) rule specifies syntax that is useful only when at least one other table inherits one of the tables that the `truncate` statement lists explicitly. See [this note](../ddl_alter_table#table-expr-note) for more detail. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
{{< /note >}}

## Semantics

Specify the name of the table to be truncated.

- `TRUNCATE` acquires `ACCESS EXCLUSIVE` lock on the tables to be truncated. The `ACCESS EXCLUSIVE` locking option is not yet fully supported.
- `TRUNCATE` is not supported for foreign tables.
- `CASCADE` and `RESTRICT` affect what happens when the table that the `TRUNCATE` statement targets has dependent tables. A dependent table (and, in turn, its dependent tables) have that status because they have direct or transitive foreign key constrains to the target table. `CASCADE` causes the closure of dependent tables all to be truncated. And `RESTRICT` causes the `TRUNCATE` attempt to fail if the target table has any dependent tables. This error outcome is the same even when all of the tables are empty. If neither `CASCADE` nor `RESTRICT` is written, then the effect is as if `RESTRICT` had been written.


## Example

First create a parent-child table pair and populate them:

```plpgsql
drop table if exists children cascade;
drop table if exists parents  cascade;

create table parents(k int primary key, v text not null);

create table children(
  parent_k  int  not null,
  k         int  not null,
  v         text not null,

  constraint children_pk primary key(parent_k, k),

  constraint children_fk foreign key(parent_k)
    references parents(k)
    match full
    on delete cascade
    on update restrict);

insert into parents(k, v) values (1, 'dog'), (2, 'cat'), (3, 'frog');

insert into children(parent_k, k, v) values
  (1, 1, 'dog-child-a'),
  (1, 2, 'dog-child-b'),
  (1, 3, 'dog-child-c'),
  (2, 1, 'cat-child-a'),
  (2, 2, 'cat-child-b'),
  (2, 3, 'cat-child-c'),
  (3, 1, 'frog-child-a'),
  (3, 2, 'frog-child-b'),
  (3, 3, 'frog-child-c');

select p.v as "parents.v", c.v as "children.v"
from parents p inner join children c on c.parent_k = p.k
order by p.k, c.k;
```

This is the result:

```output
 parents.v |  children.v
-----------+--------------
 dog       | dog-child-a
 dog       | dog-child-b
 dog       | dog-child-c
 cat       | cat-child-a
 cat       | cat-child-b
 cat       | cat-child-c
 frog      | frog-child-a
 frog      | frog-child-b
 frog      | frog-child-c
```

The `\d children` metacommand shows that it has a foreign key constraint to the  `parents` table.  This makes it a (transitive) dependent object of that table:

```output
Indexes:
    "children_pk" PRIMARY KEY, lsm (parent_k HASH, k ASC)
Foreign-key constraints:
    "children_fk" FOREIGN KEY (parent_k) REFERENCES parents(k) MATCH FULL ON UPDATE RESTRICT ON DELETE CASCADE
```
Notice that the effect of the `on delete cascade` clause is limited to what the `delete` statement does. It has no effect on the behavior of `truncate`. (There is no `on truncate cascade` clause.) Try `delete from parents`. It quietly succeeds and removes all the rows from both the `parents` table and the `children` table.

With all the rows that the setup code above inserts, try this:

```plpgsql
do $body$
declare
  message  text not null := '';
  detail   text not null := '';
begin
  -- Causes error 'cos "cascade" is required.
  truncate table parents;
  assert false, 'Should not get here';
exception
  -- Error 0A000
  when feature_not_supported then
    get stacked diagnostics
      message  = message_text,
      detail   = pg_exception_detail;
    assert message = 'cannot truncate a table referenced in a foreign key constraint',  'Bad message';
    assert detail  = 'Table "children" references "parents".',                          'Bad detail';
end;
$body$;
```

It finishes without error, showing that the bare `truncate table parents`, without `cascade`, fails and causes the message and hint that the code presents. Now repeat the attempt with `cascade` and observe the result:

```plpgsql
truncate table parents cascade;

select
  (select count(*) from parents) as "parents count",
  (select count(*) from children) as "children count";
```

The `truncate` statement now finishes without error. This is the result:

```
 parents count | children count
---------------+----------------
             0 |              0
```

Finally, try `truncate table parents` again. As promised, it still fails with the _0A000_ error, even though the transitively dependent table, _children_, is empty.
