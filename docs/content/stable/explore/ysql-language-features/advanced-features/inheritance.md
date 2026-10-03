---
title: Table inheritance example
headerTitle: Table inheritance
linkTitle: Table inheritance
description: Worked example of child tables that use INHERITS.
menu:
  stable:
    identifier: advanced-features-inheritance
    parent: advanced-features
    weight: 900
type: docs
---

YSQL supports table inheritance with the `INHERITS` keyword. What a child inherits, how schema changes propagate, and the current limitations are in [Table inheritance](../../../../api/ysql/the-sql-language/ddl-inherit/).

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

A query on `accounts` includes rows from the child tables:

```sql
SELECT SUM(balance) FROM accounts WHERE account_id = 10;
```

The parent can contain rows that are not in any child. Primary keys, unique constraints, and foreign keys are not inherited, so each child in this example defines its own.

### Queries and updates on data

`ONLY` restricts the statement to the named table:

```sql
SELECT SUM(balance) FROM ONLY accounts WHERE account_id = 10;

UPDATE ONLY accounts SET balance = balance + 100 WHERE account_id = 1;
```
