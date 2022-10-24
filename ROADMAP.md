- # YugabyteDB roadmap

YugabyteDB roadmap is divided in five themes:
- Dynamic workload optimization - features allowing YugabyteDB to cover every operational use case and transactional workload.
- Cloud native capabilities - features allowing YugabyteDB to run in any cloud, leveraging unique infrastructure strengths and addressing pitfalls.
- Developer productivity - features designed to make it easy for enterprise developers to build and ship with YugabyteDB.
- Autonomous operations - features designed to eliminate all overhead in deploying, scaling, and maintaining YugabyteDB.
- Security & complience - features designed to embrace a secure-first approach for any workload while simplifying compliance.

See detailed backgroud of every theme below.

- #- # Dynamic workload optimization

Coming soon (Q4 ‘22, Q1 ‘23):
- Query optimization: More query pushdowns, richer table statistics, auto-ANALYZE
- Transparent, workload-aware sharding using colocation 

Coming Later (Q2 ‘23+):
- Data mesh connecting cloud and edge, supporting data mobility and orchestration across multiple sites
- Auto-tiering to cold storage
- Query optimization: parallel query framework
- Automatic table partition management
- QoS: Fine grained control over other metrics (CPU, memory, etc.)

- #- #- # Handling smaller-scale workloads

| Feature | Timeline | Issues |
| --- | --- | --- |
| Query Optimization: Reverse scan improvement | Q4 '22 | - #12609 |
| Query Planning: Better stats (auto analyze, DocDB sampling) | Q1 '23 | - #11842 |
| Query Planning: Better cost estimates | Q1 '23 | - #11842 |
| Query Optimization:  Expressions push downs | Q1 '23 | - #11842 |
| Query Optimization: Batched Nested loop join | Q1 '23 | - #14199 |
| Query Optimization: Improved Scan and Index scan latency/perf | Q2 '23 | - #13715 |
| Query Routing: Built-in routing to colocated database | | |
| Query Optimization: Improve tablegroups performance | | |

- #- #- # Multi-tenant workloads

| Feature | Timeline | Issues |
| --- | --- | --- |
| Auto-create new tablegroup for dbs (tenant) | Q1 ‘23 | - #11665 |
| Cross-database concurrent DDLs | Q1 ‘23 | - #11665 |
| Per-tenant/database metrics | Q2 ‘23 | - #11665 |

- #- #- # Optimizing for dynamic workloads

| Feature | Timeline | Issues |
| --- | --- | --- |
| Connection Management: More active connections than PG | Q2 ‘23 | - #7985 |
| Connection Management: Better new connection latency than PG | Q2 ‘23 | - #7985 |
| Transparent workload aware sharding using colocation | Q2 ‘23 | |
| Auto-tiering to cold storage | | |
| Automatic table partition management | | |

- #- # Cloud native capabilities

Coming soon (Q4 ‘22, Q1 ‘23):
- Cost optimization: Graviton support, workload sizing estimator
- xCluster: atomicity of replicated transactions

Coming Later (Q2 ‘23+):
- xCluster: auto-propagation of DDLs, tiering unflushed updates to S3 on partitions
- Native metrics integration with external observability services (DataDog, etc.)
- Multi-cloud cost optimizer
- Geo-partitioning: simplify usage

- #- #- # xCluster: Cloud native “Golden Gate” capability

| Feature | Timeline | Issues |
| --- | --- | --- |
| Atomicity and ordering of replicated transactions | Q4 '22 | - #10976 |
| Manual DDL propagation (runbook) | Q1 ‘23 | - #11017<br/>- #7613 |
| Auto propagation of DDLs (new table/index/partitions) | Q2 ‘23 | - #10957 |
| Replication at Database granularity | Q2 ‘23 | - #10984 |
| Handling fail-back scenario (BCDR) - Planned failover | Q2 ‘23 | - #13807<br/>- #10978 |
| Handling fail-back scenario (BCDR) - Unplanned failover | Q2 ‘23 | - #13382<br/>- #13535<br/>- #13536 |

- #- #- # Simplified xCluster replication

| Feature | Timeline | Issues |
| --- | --- | --- |
| Make APIs atomic and fault tolerant | Q4 '22 | - #10977
| API to wait for replication to drain | Q4 '22 |
| API if we need to re-bootstrap replication| Q2 '23 | - #10645

- #- #- # Better troubleshooting

| Feature | Timeline | Issues |
| --- | --- | --- |
| Observability: Master, TServer RPCs | Q4 '22 | - #11866 |
| Observability: Client connections | Q4 '22 | - #11866 |
| Observability: time spent in various modules | Q4 '22 | - #11866 |
| Observability: Master, TServer RPCs | Q4 '22 | - #11866 |
| Observability: Client connections | Q4 '22 | - #11866 |
| EXPLAIN: num row/bytes read | | 
| EXPLAIN: DocDB execution time | | 
| EXPLAIN: Single vs multi-shard transaction | | 
| EXPLAIN: nodes included in execution | | 

- #- # Developer productivity

Coming soon (Q4 ‘22, Q1 ‘23):
- APIs for DBMS management
- CDC: support push to websockets and downstream OLAP
- CDC: Ordered transactions
- CDC: Support for formats like Parquet, Iceberg.
- Pessimistic locking for Serializable and Repeatable read

Coming Later (Q2 ‘23+):
- Rebase to PostgreSQL 13
- GIST indexes and PostGIS
- Multi APIs: Babelfish TSQL, Oracle, MySQL, MongoDB
- Native support for lambda applications
- Native REST API service
- CDC: Feed stored as SQL/Relational tables in YB

- #- #- # PostgreSQL compatibility

| Feature | Timeline | Issues |
| --- | --- | --- |
| Extensions: pg_cron | Q4 '22 | - #11087 |
| Pessimistic Locking - Serializable and RR | Q4 '22 | - #5683 |
| DDL Atomicity | Q1 '23 | |
| Monitoring Index backfill operation | Q1 '23 | - #10595 |
| PostgreSQL 13 support for new clusters | Q2 '23 | - #9797 |
| PostgreSQL 13 support for upgrade clusters | | |

- #- #- # Change Data Capture

| Feature | Timeline | Issues |
| --- | --- | --- |
| Before Image support | Q4 '22 | - #11854 |
| Adding tables on the fly to CDC stream | Q4 '22 | - #10921 |
| Alter table support | Q4 '22 | - #13970 |
| Transparent transition from snapshot to streaming mode | Q4 '22 | https://github.com/yugabyte/debezium-connector-yugabytedb/issues/51 |
| HA support for snapshot operation | Q4 '22 | https://github.com/yugabyte/debezium-connector-yugabytedb/issues/52 |
| CDC push to webhook | | - #11858 |
| OLAP integration (Snowflake, BigQuery, etc) | | - #11859 |
| Object store integration (S3, Minio, etc) | | - #11860 |
| Message bus integration (PubSub, Kinesis) | | - #11861 |

- #- #- # Performance advisor

_TBD_

- #- #- # Client drivers and ORMs

| Feature | Timeline | Issues |
| --- | --- | --- |
| Smart drivers for Go, Python, C | | https://github.com/yugabyte/yugabyte-db/projects/63 |
| ORM - achieve completeness (docs, sample apps) | Q4 '22 | https://github.com/yugabyte/yugabyte-db/projects/51 |
| Smart drivers for C- # | Q4 '22 | - #10826 |
| Official docs of recommended drivers for YB | Q4 '22 | |
| YB listed on ORM sites / documentation | | |
| R2DBC support (reactive drivers) | | |

- #- #- # Building applications quickly

| Feature | Timeline | Issues |
| --- | --- | --- |
| Better GraphQL/Hasura Performance | | |
| Lambda support for YSQL | | |

- #- # Autonomous operations

Coming soon (Q4 ‘22, Q1 ‘23):
- Incremental backups

Coming Later (Q2 ‘23+):
- 100% serverless
  - Auto-sizing
  - Auto-scaling in seconds
  - For both dedicated and multi-tenant tiers
- Multi-tenant offering for bursty workloads
- Cost/size estimator

- #- #- # Serverless auto-scaling infrastructure

| Feature | Timeline | Issues |
| --- | --- | --- |
| Fast horizontal auto-scaling | Q2 '23 | |

- #- #- # Backup and restore

| Feature | Timeline | Issues |
| --- | --- | --- |
| Backup of Tablegroups | Q4 '22 | - #11864 |
| Backup Restore perf enhancement | Q4 '22 | - #11864 |
| Regional Backups | Q4 '22 | - #11864 |
| Backup global objects, roles and permissions | | - #11864 |
| Table-level restore | | |
| File-level incremental backups | Q4 '22 | |

- #- #- # Point-in-time recovery (PITR)

| Feature | Timeline | Issues |
| --- | --- | --- |
| PITR with YSQL tablegroups | Q4 '22 | - #7120 |
| PITR for global objects (tablespaces, roles, permissions) | | |
| PITR with off-cluster backups | | |
| Table-level restore | | |

- #- # Security & compliance

Coming soon (Q4 ‘22, Q1 ‘23):
- Support for external identity providers using SAML 2.0, OIDC - (Okta, Azure AD) in YB Managed
- Customer Managed Encryption Key (CMK) in YB Managed
- Azure KMS
- Google KMS
- Custom roles with fine-grained RBAC in YB Managed
- ISO 27001 Stage 1 Audit
- SOC 2 Type 2 Issuance
- HiTrust Readiness Assessment
- PCI DSS Readiness Assessment

Coming Later (Q2 ‘23+):
- Per-tenant encryption
- ISO 27001 Stage 2 Audit
- ISO 27001 Certification Issuance
- HiTrust Certification Issuance
- PCI DSS Level 1 Authorization Issuance
- Multi-factor authentication

- #- #- # Integration with third-party security products

| Feature | Timeline | Issues |
| --- | --- | --- |
| Azure Key Vault | Q4 '22 | |
| Google Cloud KMS | Q4 '22 | |
| Hashicorp Vault - Storage backend | | |
| Hashicorp Vault - Dynamic secrets | | |
| Hashicorp Vault - Storage backend | | |
| Keycloak integration | | |
