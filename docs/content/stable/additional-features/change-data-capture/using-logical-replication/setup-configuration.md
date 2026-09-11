---
title: Setup and Configuration for Logical Replication CDC
headerTitle: Setup and configuration
linkTitle: Setup and configuration
description: Configure YugabyteDB for Change Data Capture with logical replication.
headcontent: Set up your CDC environment and tune for your use case
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/setup-configuration/
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: setup-configuration
    weight: 30
type: docs
---

## Quick Reference: All Configuration Flags

| Category | Flag | Purpose | Default | Versions |
|----------|------|---------|---------|----------|
| **Data Model** | | | | |
| | `ysql_yb_default_replica_identity` | Default replica identity for new tables | DEFAULT | All |
| | `ysql_yb_enable_implicit_dynamic_tables_logical_replication` | Auto-detect publication changes | true | v2026.1+ |
| **Schema Evolution** | | | | |
| | `enable_table_rewrite_for_cdcsdk_table` | Allow DDLs that cause table rewrites | true | v2026.1+ |
| | `cdcsdk_enable_dynamic_table_support` | Support dynamic table schema changes | false | v2025.1+ |
| **Publication Management** | | | | |
| | `cdcsdk_publication_list_refresh_interval_secs` | How often to check for table changes | 900 (15 min) | All |
| **Data Retention** | | | | |
| | `cdc_wal_retention_time_secs` | How long to keep WAL for CDC | 28800 (8 h) | All |
| | `cdc_intent_retention_ms` | How long to retain row history | 28800000 (8 h) | All |
| | `cdc_default_retention_hours` | Total retention window | 8 hours | v2024.2.1+ |
| **Before-Image Handling** | | | | |
| | `cdc_enable_intra_transactional_before_image` | Capture before-image in same transaction | false | v2025.2.4.0+ |
| | `cdc_send_null_before_image_if_not_exists` | Send null if before-image unavailable | false | v2025.1+ |
| **Performance Tuning** | | | | |
| | `cdcsdk_max_consistent_records` | Max records per response | 100 | All |
| | `cdcsdk_vwal_getchanges_resp_max_size_bytes` | Max response size in bytes | 52428800 (50 MB) | All |
| **Critical Restrictions** | | | | |
| | `ysql_yb_ddl_transaction_block_enabled` | Enable transactional DDL | false | All |

{{< warning title="Important" >}}
Do NOT enable `ysql_yb_ddl_transaction_block_enabled` when using CDC. This flag is incompatible with logical replication.
{{< /warning >}}

## Prerequisites

### System Requirements

- All YB-TServer nodes must be accessible to CDC consumers
- Network latency to YB-TServer: < 100ms recommended for low-latency CDC
- Disk space: Monitor WAL retention; allow 2-3x your typical WAL generation rate
- Memory: Each slot consumes ~50-100 MB depending on table count

### Essential Configuration Steps

#### 1. Set Required Flags on All YB-TServers and YB-Masters

```bash
# For v2026.1 and later (recommended):
yb-tserver --ysql_yb_enable_implicit_dynamic_tables_logical_replication=true
yb-tserver --enable_table_rewrite_for_cdcsdk_table=true

# For v2025.1-v2025.2:
yb-tserver --ysql_yb_enable_implicit_dynamic_tables_logical_replication=false
yb-tserver --cdcsdk_enable_dynamic_table_support=false

# All versions:
yb-tserver --ysql_yb_ddl_transaction_block_enabled=false
yb-tserver --ysql_yb_default_replica_identity=FULL
```

Or use `yb-ts-cli` to set flags at runtime:

```bash
./yb-ts-cli --server_address=<tserver-ip:9100> \
  set_flag ysql_yb_enable_implicit_dynamic_tables_logical_replication true
```

#### 2. Verify Configuration

```sql
-- Check that flags are set correctly
SELECT name, setting FROM pg_settings 
WHERE name IN (
  'ysql_yb_enable_implicit_dynamic_tables_logical_replication',
  'enable_table_rewrite_for_cdcsdk_table',
  'cdc_wal_retention_time_secs'
);

-- Verify cluster is ready for CDC
SELECT * FROM yb_servers();
```

#### 3. Set Table Replica Identity

For each table you want to replicate, set the appropriate replica identity:

```sql
-- FULL: Captures all columns in before-image (most flexible, highest overhead)
ALTER TABLE orders REPLICA IDENTITY FULL;

-- CHANGE: Only changed columns (recommended for most use cases)
ALTER TABLE orders REPLICA IDENTITY USING INDEX orders_pkey;

-- DEFAULT: Primary key only (lowest overhead but limited before-image)
ALTER TABLE orders REPLICA IDENTITY DEFAULT;
```

## Configuration by Use Case

### Production CDC with Kafka Connect

**Goals:** High throughput, reliable offsets, minimal data loss

**Configuration:**

```bash
# On all YB-TServers
ysql_yb_enable_implicit_dynamic_tables_logical_replication = true
enable_table_rewrite_for_cdcsdk_table = true
cdcsdk_enable_dynamic_table_support = true
cdc_wal_retention_time_secs = 28800  # 8 hours
cdc_intent_retention_ms = 28800000
cdcsdk_max_consistent_records = 1000
cdcsdk_vwal_getchanges_resp_max_size_bytes = 104857600  # 100 MB
ysql_yb_ddl_transaction_block_enabled = false
```

**Monitoring:**
- Check Kafka Connect lag regularly: should be < 1 second
- Monitor YB-TServer memory: watch for growth in intent retention
- Set up alerts: if consumer lag exceeds retention window, slot becomes invalid

### Development and Testing

**Goals:** Fast table changes, minimal resource usage, easy cleanup

**Configuration:**

```bash
# On all YB-TServers
ysql_yb_enable_implicit_dynamic_tables_logical_replication = true
enable_table_rewrite_for_cdcsdk_table = true
cdcsdk_enable_dynamic_table_support = true
cdc_wal_retention_time_secs = 3600  # 1 hour
cdc_intent_retention_ms = 3600000
cdcsdk_publication_list_refresh_interval_secs = 60  # Check more frequently for new tables
```

**Benefits:**
- Shorter retention window saves disk space
- Frequent publication refresh allows quick table additions
- Reset test data easily since retention is short

### Long Retention (Near PITR)

**Goals:** Retain changes for extended period, support late-arriving consumers

**Configuration:**

```bash
# On all YB-TServers
cdc_wal_retention_time_secs = 604800  # 7 days
cdc_intent_retention_ms = 604800000
cdc_default_retention_hours = 168
ysql_yb_default_replica_identity = FULL
```

{{< warning title="Important" >}}
This configuration significantly increases disk usage and may degrade read performance. Monitor:
- Disk space: `du -sh $YB_DATA_DIR/yb-data/tserver/wal`
- Read latency: Queries may slow down due to compaction halting
- YB-TServer memory: Retention metadata grows over time
{{< /warning >}}

## Understanding Data Retention

### How Retention Works

```
Transaction T1 occurs at LSN 1000
    ↓
CDC marks resources (WAL, row history) as "in-use"
    ↓
Consumer's replication slot reads LSN 1000
    ↓
Consumer processes and acknowledges LSN 1000
    ↓
Resources released for cleanup/compaction
    ↓
If consumer falls behind, resources retained until timeout
    ↓
After cdc_intent_retention_ms, resources garbage collected
    ↓
Slot becomes unusable (must recreate)
```

### Replica Identity Impact on Performance

When using FULL or DEFAULT replica identity, CDC must preserve row history for UPDATE/DELETE operations. This is done by suspending the compaction process.

| Replica Identity | Before-Image Size | Compaction Impact | Read Performance |
|------------------|-------------------|--------------------|------------------|
| **FULL** | All columns | Compaction halted | Can degrade significantly if consumer lags |
| **CHANGE** | Changed columns only | Minimal impact | Minimal degradation |
| **DEFAULT** | Primary key only | Minimal impact | Minimal degradation |

**Critical:** If using FULL or DEFAULT and consumer falls behind retention window, compaction backlog grows, causing read latency to increase dramatically.

**Recommendation:** Use CHANGE identity for production, monitor consumer lag closely.

## Advanced Tuning

### High-Throughput Configuration

For maximum throughput at the expense of latency:

```bash
cdcsdk_max_consistent_records = 10000
cdcsdk_vwal_getchanges_resp_max_size_bytes = 268435456  # 256 MB
cdc_wal_retention_time_secs = 43200  # 12 hours
```

**Tradeoff:** Larger batch sizes mean fewer round-trips and higher throughput, but consumers need more memory to buffer records.

### Low-Latency Configuration

For minimum latency at the expense of throughput:

```bash
cdcsdk_max_consistent_records = 100
cdcsdk_vwal_getchanges_resp_max_size_bytes = 1048576  # 1 MB
cdc_wal_retention_time_secs = 14400  # 4 hours
```

**Tradeoff:** Smaller batches deliver changes faster but require more network round-trips.

### Dynamic Publication Refresh Tuning

For faster detection of new tables (at the expense of overhead):

```bash
# Temporarily lower refresh interval
./yb-ts-cli --server_address=<tserver-ip:9100> \
  set_flag cdcsdk_publication_list_refresh_interval_secs 60

# After new table is detected and streaming:
./yb-ts-cli --server_address=<tserver-ip:9100> \
  set_flag cdcsdk_publication_list_refresh_interval_secs 900
```

{{< note title="Note" >}}
Every refresh incurs overhead. Lower the interval only temporarily when adding tables, then restore the default.
{{< /note >}}

## Version Upgrades

### Upgrading to v2026.1

**New capabilities:**
- `enable_table_rewrite_for_cdcsdk_table` (default: true) - allows non-blocking DDL for table rewrites
- `ysql_yb_enable_implicit_dynamic_tables_logical_replication` (default: true) - immediate publication change detection

**Upgrade steps:**

1. Update flags on all YB-Masters and YB-TServers:
   ```bash
   enable_table_rewrite_for_cdcsdk_table = true
   ysql_yb_enable_implicit_dynamic_tables_logical_replication = true
   ```

2. Rolling restart (one at a time):
   ```bash
   # Stop YB-Master node
   # Start YB-Master node
   # Verify node rejoined cluster
   # Repeat for each node
   
   # Then repeat for each YB-TServer
   ```

3. Verify new behavior:
   ```sql
   -- This should NOT block now (if non-colocated table):
   ALTER TABLE users ALTER COLUMN age TYPE BIGINT;
   ```

### Upgrading to v2025.2.4.0+

**New capability:** `cdc_enable_intra_transactional_before_image` - captures before-images for rows updated/deleted in same transaction as insert

**When to enable:**

```bash
# If you have this pattern:
BEGIN;
  INSERT INTO users VALUES (1, 'John');
  UPDATE users SET age = 30 WHERE id = 1;
COMMIT;

# Without flag: CDC errors on UPDATE
# With flag: CDC captures before-image (John, NULL) -> (John, 30)
```

**Enable after testing:**

```bash
cdc_enable_intra_transactional_before_image = true
```

## Limitations by Version

| Feature | v2024.2 | v2025.1 | v2025.2 | v2026.1 |
|---------|---------|---------|---------|---------|
| **Replica Identity** | PK only | Full support | Full support | Full support |
| **Table Schema Support** | Limited | Limited | Limited | Full (non-colocated) |
| **DDL Rewrite Blocking** | Blocked | Blocked | Blocked | Non-blocking |
| **Intra-txn Before-Image** | Manual (2024.2.9.1+) | Manual | Auto (2025.2.4.0+) | Auto |
| **Savepoints** | No (until 2024.2.8) | No (until 2025.1.4) | Yes (2025.2.2.0+) | Yes |
| **Implicit Publication Changes** | N/A | No | No | Yes (default) |
| **Colocated Table Rewrites** | Blocked | Blocked | Blocked | Blocked |

## Validation Checklist

- [ ] All required gflags set on every YB-TServer and YB-Master
- [ ] Settings persisted after server restart verification
- [ ] Replica identity set correctly on all replicated tables
- [ ] Retention window appropriate for your use case
- [ ] `ysql_yb_ddl_transaction_block_enabled = false` confirmed
- [ ] Cluster health verified: `SELECT * FROM yb_servers();`
- [ ] Test replication slot creation: `CREATE REPLICATION SLOT test_slot FOR PUBLICATION test_pub USING cdcsdk;`
- [ ] Test publication creation: `CREATE PUBLICATION test_pub FOR ALL TABLES;`
- [ ] Consumer connectivity confirmed from application network

## Troubleshooting Configuration Issues

### Slot Creation Fails: "ysql_yb_enable_implicit_dynamic_tables_logical_replication not set"

**Solution:** Set flag on all YB-TServers (not just one):
```bash
for tserver in tserver1 tserver2 tserver3; do
  yb-ts-cli --server_address=$tserver:9100 \
    set_flag ysql_yb_enable_implicit_dynamic_tables_logical_replication true
done
```

### DDL Blocking When It Shouldn't

**Symptom:** ALTER TABLE fails with "operation blocked by active CDC" even on v2026.1

**Cause:** Flag not set or not propagated to all nodes

**Solution:**
```bash
# Verify flag is true everywhere:
SELECT name, setting FROM pg_settings 
WHERE name = 'enable_table_rewrite_for_cdcsdk_table';

# Set on all TServers if not true:
./yb-ts-cli --server_address=<tserver:9100> \
  set_flag enable_table_rewrite_for_cdcsdk_table true
```

### High Memory Usage

**Symptom:** YB-TServer memory grows constantly

**Cause:** Consumer not keeping up with production; intent retention holding too much history

**Solution:**
1. Check consumer lag: `SELECT * FROM pg_replication_slots;`
2. If lag > retention window, consumer will disconnect
3. Increase retention if needed: `cdc_intent_retention_ms = 86400000` (24 hours)
4. Or speed up consumer

### Read Latency Degradation

**Symptom:** Normal queries become slow when CDC active

**Cause:** Replica identity set to FULL, compaction halted due to retention

**Solution:**
1. Check replica identity: `SELECT relname, relreplident FROM pg_class WHERE relname = 'your_table';`
2. Change to CHANGE if possible: `ALTER TABLE your_table REPLICA IDENTITY USING INDEX your_table_pkey;`
3. Or ensure consumer keeps up with production
