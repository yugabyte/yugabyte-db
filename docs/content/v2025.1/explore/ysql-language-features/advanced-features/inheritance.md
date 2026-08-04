---
title: Table inheritance
linkTitle: Table inheritance
description: Table inheritance in YSQL
menu:
  v2025.1:
    identifier: advanced-features-inheritance
    parent: advanced-features
    weight: 900
tags:
  feature: tech-preview
type: docs
---

YSQL supports table inheritance using the INHERITS keyword, which is a [PostgreSQL feature](https://www.postgresql.org/docs/current/ddl-inherit.html) that allows you to create child tables that inherit columns and certain constraints from one or more parent tables.

## Example

To illustrate with a basic example, create the following tables:

```sql
-- Columns common to all account types
CREATE TABLE accounts (
    account_id INTEGER PRIMARY KEY,
    balance NUMERIC NOT NULL CHECK (balance >= 0),
    profit NUMERIC DEFAULT 0
);

-- Child table for investment accounts
CREATE TABLE investment_accounts (
    investment_type TEXT NOT NULL CHECK (investment_type IN ('stocks', 'bonds', 'funds')),
    CHECK (balance >= 5000),
    PRIMARY KEY (account_id, investment_type)
) INHERITS (accounts);

-- Child table for savings accounts
CREATE TABLE savings_accounts (
    interest_rate NUMERIC NOT NULL CHECK (interest_rate >= 0 AND interest_rate <= 0.1),
    CHECK (balance >= 100),
    PRIMARY KEY (account_id)
) INHERITS (accounts);
```

```sql
testdb=# \d investment_accounts
```

```output
             Table "public.investment_accounts"
     Column      |  Type   | Collation | Nullable | Default
-----------------+---------+-----------+----------+---------
 account_id      | integer |           | not null |
 balance         | numeric |           | not null |
 profit          | numeric |           |          | 0
 investment_type | text    |           | not null |
Indexes:
    "investment_accounts_pkey" PRIMARY KEY, lsm (account_id HASH, investment_type ASC)
Check constraints:
    "accounts_balance_check" CHECK (balance >= 0::numeric)
    "investment_accounts_balance_check" CHECK (balance >= 5000::numeric)
    "investment_accounts_investment_type_check" CHECK (investment_type = ANY (ARRAY['stocks'::text, 'bonds'::text, 'funds'::text]))
Inherits: accounts
```

This schema allows for certain queries to be performed over all accounts while still preserving features unique to each account type. For example:

```sql
SELECT SUM(balance) FROM accounts WHERE account_id = 10;
```

Any columns added to or dropped from the parent `accounts` table are propagated to child tables so that such queries on the parent accounts table are always well formed.

However, there are certain caveats to keep in mind:

1. The parent table `accounts` may have its own rows that are not part of any child tables.
1. The primary key for `account_id` on the parent `accounts` table does not propagate to children and has to be redefined for each child table. This is also the behavior for foreign key constraints and non-primary key unique constraints. You need to take special care to maintain such constraints across parent-child hierarchies.

Table inheritance can lead to complex hierarchies similar to class inheritance in object-oriented programming because a specific table can inherit from multiple parent tables and can itself be a parent table for other child tables.

### Queries and updates on data

SELECT and UPDATE queries on the parent table operate on a union of the parent and all child tables in the hierarchy. To restrict queries to just the specific table, use the ONLY keyword:

```sql
SELECT SUM(balance) FROM ONLY accounts WHERE account_id = 10;

UPDATE ONLY accounts SET balance = balance + 100 WHERE account_id = 1;
```

## Schema changes

1. Adding columns to or dropping columns from  or altering columns on the parent table propagates to all children in the hierarchy. The behavior of such operations can be more complex when a child table has a column of the same name and type before establishing inheritance or when a child table inherits from multiple parent tables with slightly differing definitions of a column (for example, NULL vs NOT NULL constraints on the column). For more details on the allowed differences in column definitions and the resolution of such differences in inheritance, [consult the PostgreSQL documentation](https://www.postgresql.org/docs/current/ddl-inherit.html).
2. Adding or dropping certain kinds of constraints propagates to all children in the hierarchy. The exceptions are primary key constrains, unique constraints and foreign key constraints. For more details, consult the [PostgreSQL documentation](https://www.postgresql.org/docs/current/ddl-inherit.html).
3. For certain schema changes, the `ONLY` keyword can be used to restrict the schema change to just the parent table. For example, `ALTER TABLE ONLY accounts DROP COLUMN profit` drops the column from the parent table alone while leaving it on the child tables.

## Limitations

Table inheritance is {{<tags/feature/tp idea="2158">}} - report any problems using issue {{<issue 27949>}}.

- Dropping or adding a column to a parent fails when "local" column on child table exists. (Issue {{<issue 26094>}})
- Crash while obtaining a row lock on a parent inheritance table with a child file_fdw table. (Issue {{<issue 27105>}})

For an up-to-date list, see issue {{<issue 27949>}}.
