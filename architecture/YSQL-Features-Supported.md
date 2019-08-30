# What Features Does YSQL Support?

YSQL uses the query layer from PostgreSQL v11.2, and intends to support most PostgreSQL functionality on top of a distributed database. This document tracks what features are currently supported, and which ones are in the Roadmap.

> **Note**: This list is updated periodically but not always up-to-date. Please ask in our [community Slack chat](https://www.yugabyte.com/slack) if you have any questions about specifics or the latest state. We release software almost once a week, and features are rapidly getting added.

## PostgreSQL Feature Support

Here are the features currently supported as of YugaByte DB v1.3, Jul 15 2019. This list also indicates what is planned for YugaByte DB v2.0 coming out around the end of August.

- [ ] All data types
    - [x] Basic types
    - [x] Autoincrement IDs (various `SERIAL` types)
    - [x] Arrays, NULLs in Arrays
    - [x] `JSONB`, `JSONB`-modifying operators and functions
    - [ ] Enum ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
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
    - [ ] Authentication ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Role based access control ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
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
    - [x] Stored Procedure
    - [x] Triggers
    - [x] Views
    - [x] Extensions
    - [ ] Materialized Views
    - [ ] Generic extension framework ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Foreign data wrappers

## Core DB Features

- [ ] Change data capture
    - [ ] Kafka integration ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Elastic search integraion ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
- [ ] Two DC deployment
    - [ ] Master-slave (asynchronous replication) ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Multi-master (bidirectional replication, last writer wins semantics) ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
- [ ] Security
    - [ ] SSL Support/TLS Encryption ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Encryption at rest ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
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
    - [ ] GraphQL support ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] Advanced Spring support ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
- [ ] Tools support
    - [x] psql
    - [ ] pgAdmin ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] TablePlus
    - [ ] pg_dump ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)
    - [ ] pgbench ![Generic badge](https://img.shields.io/badge/Target-v2.0-green.svg)

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/YSQL-Features-Supported.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)
