# What Features Does YSQL Support?

YSQL uses the query layer from PostgreSQL v11.2, and intends to support most PostgreSQL functionality on top of a distributed database. This document tracks what features are currently supported, and which ones are in the Roadmap.

> **Note**: This list is updated periodically but not always up-to-date. Please ask in our [community Slack chat](https://www.yugabyte.com/slack) if you have any questions about specifics or the latest state. We release software almost once a week, and features are rapidly getting added.

## PostgreSQL Feature Support

Here are the features currently supported as of YugaByte DB v2.0, Jan 15 2020. This list also indicates what is planned for YugaByte DB v2.1 coming out around the beginning of February.

- [ ] All data types
    - [x] Basic types
    - [x] Autoincrement IDs (various `SERIAL` types)
    - [x] Arrays, NULLs in Arrays
    - [x] `JSONB`, `JSONB`-modifying operators and functions
    - [x] User-defined types
        - [x] Base types
        - [x] Enumerated types
        - [x] Range types
        - [x] Composite (record) types
        - [x] Array types
    - [ ] XML data type
- [x] DDL Statements
    - [x] Tables with UNIQUE KEY, PRIMARY KEY, CONSTRAINTS
    - [x] Temporary tables
    - [x] ALTER table to add columns
- [x] Queries
    - [x] FROM, WHERE, GROUP BY, HAVING, DISTINCT, LIMIT/OFFSET, WITH queries
    - [x] `EXPLAIN` query plans
    - [x] JOINs (INNER/OUTER, LEFT/RIGHT)
    - [x] Expressions and Operators
    - [x] Common Table Expressions (CTE) and Recursive Queries
    - [x] Upserts (`INSERT ... ON CONFLICT DO NOTHING/UPDATE`)
    - [x] Window functions
    - [x] Authentication
    - [x] Role based access control
    - [x] Row-level security
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
- [ ] Transactions
    - [x] TRANSACTION blocks
    - [x] Serializable and Snapshot Isolation levels
    - [ ] Savepoints
- [ ] Advanced SQL Support
    - [x] Stored procedures
    - [x] User-defined functions
    - [x] Triggers
        - [x] Row-level triggers (BEFORE, AFTER, INSTEAD OF)
        - [x] Statement-level triggers (BEFORE, AFTER, INSTEAD OF)
        - [ ] Deferrable triggers
        - [ ] Transition tables (REFERENCING clause for triggers)
    - [x] Views
    - [x] Extensions ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
        - [x] pgcrypto
        - [x] fuzzystrmatch
        - [x] spi module
    - [x] Generic extension framework
    - [ ] Materialized Views
    - [ ] Foreign data wrappers

## Core DB Features

- [ ] Change data capture
    - [x] Kafka integration ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [ ] Elastic search integration ![Generic badge](https://img.shields.io/badge/Target-v2.2-green.svg)
- [ ] Two DC deployment
    - [x] Master-slave (asynchronous replication)
    - [x] Multi-master (bidirectional replication, last writer wins semantics)
- [ ] Security
    - [x] SSL Support/TLS Encryption 
    - [x] Encryption at rest ![Generic badge](https://img.shields.io/badge/Target-v2.1-green.svg)
- [ ] Read replica support


## App Development Support

- [x] Client driver support
    - [x] Java, Go
    - [x] NodeJS
    - [x] Python
    - [x] Ruby
    - [x] C#
    - [x] C++
    - [x] C
    - [x] Rust
- [x] ORM support
    - [x] Spring
    - [x] Hibernate
    - [x] Play
    - [x] Gorm
    - [x] Sequelize
    - [x] SQLAlchemy
    - [x] ActiveRecord
    - [x] EntityFramework
    - [x] Diesel
- [ ] Ecosystem
    - [x] GraphQL support ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [x] Advanced Spring support ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
- [ ] Tools support
    - [x] psql
    - [x] pg_dump
    - [x] pgbench
    - [x] DBeaver
    - [x] pgAdmin ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [x] TablePlus ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/YSQL-Features-Supported.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)
