---
title: Table inheritance
linkTitle: Table inheritance
description: Table inheritance in YSQL
menu:
  preview:
    identifier: advanced-features-inheritance
    parent: advanced-features
    weight: 400
type: docs
---


## Overview

[BETA] YSQL supports table inheritance, which is a [PostgreSQL feature](https://www.postgresql.org/docs/current/ddl-inherit.html) that allows users to create child tables that inherit columns and certain constraints from one or more parent tables.  

### Example

To illustrate with a simple example,

```
-- Columns common to all account types
CREATE TABLE accounts (
    account_id INTEGER PRIMARY KEY,
    balance NUMERIC NOT NULL CHECK (balance >= 0),
    gains NUMERIC DEFAULT 0
);

-- Child table for investment accounts
CREATE TABLE investment_accounts (
    investment_type TEXT NOT NULL CHECK (investment_type IN ('stocks', 'bonds', 'funds')),
    CHECK (balance >= 5000),
    PRIMARY KEY (account_id, investment_type)  -- Composite primary key
) INHERITS (accounts);

-- Child table for savings accounts
CREATE TABLE savings_accounts (
    interest_rate NUMERIC NOT NULL CHECK (interest_rate >= 0 AND interest_rate <= 0.1),
    CHECK (balance >= 100),
    PRIMARY KEY (account_id)  -- Single-column primary key
) INHERITS (accounts);

testdb=# \d investment_accounts
             Table "public.investment_accounts"
     Column      |  Type   | Collation | Nullable | Default
-----------------+---------+-----------+----------+---------
 account_id      | integer |           | not null |
 balance         | numeric |           | not null |
 gains           | numeric |           |          | 0
 investment_type | text    |           | not null |
Indexes:
    "investment_accounts_pkey" PRIMARY KEY, lsm (account_id HASH, investment_type ASC)
Check constraints:
    "accounts_balance_check" CHECK (balance >= 0::numeric)
    "investment_accounts_balance_check" CHECK (balance >= 5000::numeric)
    "investment_accounts_investment_type_check" CHECK (investment_type = ANY (ARRAY['stocks'::text, 'bonds'::text, 'funds'::text]))
Inherits: accounts
```

This schema allows for certain queries to be performed over all accounts while still preserving features unique to each account type. For example,

```
SELECT SUM(balance) FROM accounts WHERE account_id = 10;
```

Any columns added to/dropped from the parent `accounts` table are propagated to child tables so that such queries on the parent accounts table are always well formed. 

However, there are certain caveats to keep in mind here.
1. The parent table `accounts` may have its own rows that are not part of any child tables.
2. The primary key for account_id on the parent `accounts` table does not propagate to children and has to be redefined for each child table. This is also true for foreign key constraints and non-primary key unique constaints. Special care has to be taken to maintain such constraints across parent-child hierarchies.

Table inheritance can lead to complex hierarchies similar to class inheritance in object-oriented programming because a specific table can inherit from multiple parent tables and can itself be a parent table for other child tables.

### Queries and updates on data

SELECT and UPDATE queries on the parent table operate on a union of the parent and all child tables in the hierarchy. To restrict queries to just the specific table, the `ONLY` keyword can be used.

```
SELECT SUM(balance) FROM ONLY accounts WHERE account_id = 10;

UPDATE ONLY accounts SET balance = balance + 100 WHERE account_id = 1;
```

## Schema changes

1. Adding columns to or dropping columns from  or altering columns on the parent table propagates to all children in the hierarchy. The behavior of such operations can be more complex when a child table has a column of the same name and type before establishing inheritance or when a child table inherits from multiple parent tables with slightly differing definitions of a column (for example, NULL vs NOT NULL constraints on the column). For more details on the allowed differences in column definitions and the resolution of such differences in inheritance, [consult the PostgreSQL documentation](https://www.postgresql.org/docs/current/ddl-inherit.html).
2. Adding or dropping certain kinds of constraints propagates to all children in the hierarchy. The exceptions are primary key constrains, unique constraints and foreign key constraints. For more details, consult the [PostgreSQL documentation](https://www.postgresql.org/docs/current/ddl-inherit.html)
3. For certain schema changes, the `ONLY` keyword can be used to restrict the update to just the parent table. For example, `ALTER TABLE ONLY accounts DROP COLUMN gains` drops the column from the parent table alone while leaving it on the child tables.


## Limitations


https://github.com/yugabyte/yugabyte-db/issues/26094
https://github.com/yugabyte/yugabyte-db/issues/27105

For a more up to date list, see https://github.com/yugabyte/yugabyte-db/issues/5956.

Note that table inheritance is a [BETA] feature. Please report any issues encountered at https://github.com/yugabyte/yugabyte-db/issues/5956.
