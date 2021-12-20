# What Features Does YSQL Support?

YSQL uses the query layer from **PostgreSQL v11.2**, and intends to support most PostgreSQL functionality on top of a distributed database. This document tracks what features are currently supported, and which ones are in the Roadmap.

> **Note**: This list is updated periodically but not always up-to-date. Please ask in our [community Slack chat](https://www.yugabyte.com/slack) if you have any questions about specifics or the latest state. We release software almost once a week, and features are rapidly getting added.

## PostgreSQL Feature Support

Here are the features currently supported as of YugabyteDB v2.11, Nov 2021. 

- [ ] Upgrade to recent PostgeSQL 
    - [x] v11.2 
    - [ ] v13 ![Generic badge](https://img.shields.io/badge/Target-v2.13-green.svg)
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
        - [x] Domain types
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
- [x] All Constraints
    - [x] Primary Key
    - [x] Foreign Key
    - [x] CHECK, NOT NULL, other basic constraints
    - [x] Deferrable Foreign Key constraints
    - [ ] Deferrable Primary Key and Unique constraints
    - [ ] Exclusion constraints
- [ ] Indexes
    - [x] Secondary indexes
    - [x] Partial indexes
    - [x] GIN Indexes
    - [ ] GIST Indexes
- [ ] Transactions
    - [x] TRANSACTION blocks
    - [x] Serializable and Snapshot Isolation levels
    - [x] Savepoints
- [ ] Advanced SQL Support
    - [x] Stored procedures
    - [x] User-defined functions
    - [x] Triggers
        - [x] Row-level triggers (BEFORE, AFTER, INSTEAD OF)
        - [x] Statement-level triggers (BEFORE, AFTER, INSTEAD OF)
        - [ ] Deferrable triggers
        - [ ] Transition tables (REFERENCING clause for triggers)
    - [x] Views
    - [x] Extensions
        - [x] pgcrypto
        - [x] fuzzystrmatch
        - [x] spi module
        - [x] pg_stat_statement
        - [x] orafce
        - [x] pg_stat_monitor
        - [x] pg_audit 
    - [x] Generic extension framework
    - [x] Partitions support
    - [x] Foreign data wrappers
    - [ ] Materialized Views ![Generic badge](https://img.shields.io/badge/Target-v2.13-green.svg)


## Core DB Features

- [x] Security
    - [x] Authentication
    - [x] Role based access control
    - [x] Row-level security
    - [x] SSL Support/TLS Encryption 
    - [x] Encryption at rest
- [x] Backup and restore
    - [x] Scan based backups using `ysql_dump`
    - [x] Distributed backups 
- [x] Distributed, dynamic rebuilding of indexes
    - [x] Support for simple indexes
    - [x] Support for unique constraints
- [x] Sharding and splitting
    - [x] Hash sharding support
    - [x] Range sharding support
    - [x] Dynamic tablet splitting
- [ ] Change data capture
    - [x] Kafka integration ![Generic badge](https://img.shields.io/badge/Target-v2.11.2-green.svg)
    - [ ] Generic client library for consumers
    - [ ] Elastic search integration
- [x] Colocated tables
- [x] Geo-distributed deployments
    - [x] Three or more DCs (with sync replication)
    - [x] Master-slave (asynchronous replication)
    - [x] Multi-master (bidirectional replication, last writer wins semantics)
    - [X] Follower reads
    - [x] Read replica support
    - [x] Row level geo-partitioning


## App Development Support

- [x] Client driver support
    - [x] Java
    - [x] Go
    - [x] NodeJS
    - [x] Python
    - [x] Ruby
    - [x] C#
    - [x] C++
    - [x] C
    - [x] Rust
- [x] ORM support
    - [x] Spring (Java)
    - [x] Hibernate (Java)
    - [x] Play (Scala)
    - [x] Gorm (Go)
    - [x] Sequelize (NodeJS)
    - [x] SQLAlchemy (Python)
    - [x] ActiveRecord (Ruby on Rails)
    - [x] EntityFramework (.NET)
    - [x] Diesel (Rust)
    - [x] TypeORM (JavaScript)
    - [x] Dapper (.NET)
    - [x] MyBatis (Java)
    - [ ] Django (Python)![Generic badge](https://img.shields.io/badge/Target-v2.13-green.svg)
    - [ ] Laravel (PHP)![Generic badge](https://img.shields.io/badge/Target-v2.13-green.svg)
    - [ ] jOOQ (Java)![Generic badge](https://img.shields.io/badge/Target-v2.13-green.svg)
    - [ ] Doctrine (PHP)
    - [ ] PonyORM (Python)
    - [ ] upper/db (Go)
    - [ ] xorm (Go)
    - [ ] mikro-orm (NodeJS)
- [x] Schema migration
    - [x] Flyway
    - [x] Liquibase
    - [x] SchemaHero
    - [ ] Alembic
    - [ ] DbUp
- [ ] Ecosystem
    - [x] GraphQL support ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [x] Spring Data Yugabyte 
- [ ] Tools support
    - [x] psql
    - [x] pg_dump
    - [x] pgbench
    - [x] DBeaver
    - [x] pgAdmin ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [x] TablePlus ![Generic badge](https://img.shields.io/badge/Status-BETA-blue.svg)
    - [ ] DBeaver
    - [ ] DBSchema
    - [ ] SQL Workbench/J
    - [ ] Visual Studio Code
    - [ ] Beekeeper Studio
    - [ ] DbVisualizer
    - [ ] Navicat for PostgreSQL
    - [ ] Arctype
    - [ ] Pgweb
    - [ ] Postico
    - [ ] Metabase
    - [ ] Datagrip

## Performance
- [ ] Parallel queries
- [ ] YCSB
    - [x] Easy to run YCSB package
    - [ ] Framework to publish YCSB performance numbers with each release
    - [ ] Available as rpm/deb/container/yum/brew
- [ ] TPCC
    - [x] Easy to run TPCC package
    - [ ] Framework to publish TPCC performance numbers with each release
    - [ ] Available as rpm/deb/container/yum/brew

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/YSQL-Features-Supported.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
