# What Features Does YSQL Support?

YSQL uses the query layer from PostgreSQL v11.2, and intends to support most PostgreSQL functionality on top of a distributed database. This document tracks what features are currently supported, and which ones are in the Roadmap.

> **Note**: This list is updated periodically but not always up-to-date. Please ask in our [community Slack chat](https://www.yugabyte.com/slack) if you have any questions about specifics or the latest state. We release software almost once a week, and features are rapidly getting added.

## PostgreSQL Features Supported

- [ ] All data types
    - [x] Basic types
    - [x] Autoincrement IDs (various `SERIAL` types)
    - [x] Arrays, NULLs in Arrays
    - [x] `JSONB`, `JSONB`-modifying operators and functions
    - [ ] Enum
    - [ ] XML data type
- [x] All Constraints
    - [x] Primary Key
    - [x] Foreign Key
    - [x] CHECK, NOT NULL, other basic constraints
    - [ ] Deferrable constraints
    - [ ] Exclusion constraints
- [ ] Indexes
    - [x] Secondary indexes
    - [x] Partial indexes
    - [ ] GIN Indexes
    - [ ] GIST Indexes
- [ ] SQL
    - [x] Common Table Expressions (CTE) and Recursive Queries
    - [x] Upserts (`INSERT ... ON CONFLICT DO NOTHING/UPDATE`)
- [ ] Transactions
    - [x] TRANSACTION blocks
    - [x] Serializable and Snapshot Isolation levels
    - [ ] Savepoints
- [x] Expressions and Operators
- [x] JOINS
- [x] Stored Procedure
- [x] Triggers
- [x] Views
- [ ] Materialized Views
- [ ] Authentication and RBAC
- [ ] Extensions
- [ ] PostGIS

